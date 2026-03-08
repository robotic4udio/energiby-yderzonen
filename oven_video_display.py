#!/usr/bin/env python3
"""
Real-time video mixer for oven intensity control.
Interpolates between 4 looping oven videos based on intensity parameter (0-1).
All videos are loaded into RAM for low-latency real-time playback.
"""

import cv2
import numpy as np
from pathlib import Path
from typing import List, Optional
import tkinter as tk
import multiprocessing
import time
import argparse
import subprocess
import shutil
from pythonosc import dispatcher
from pythonosc import osc_server
from threading import Thread


def load_video_frames(path: str, frame_width: int, frame_height: int) -> Optional[List[np.ndarray]]:
    """Load all frames from a video file into RAM with resizing."""
    cap = cv2.VideoCapture(str(path))
    if not cap.isOpened():
        return None
    
    frames = []
    while True:
        ret, frame = cap.read()
        if not ret:
            break
        
        # Resize to output dimensions (using nearest neighbor for faster performance)
        frame = cv2.resize(frame, (frame_width, frame_height), interpolation=cv2.INTER_NEAREST)
        frames.append(frame)
    
    cap.release()
    return frames if frames else None


class OvenVideoMixer:
    """Real-time video mixer based on oven intensity (0-1)."""
    
    def __init__(
        self,
        video_paths: List[str],
        frame_width: int = 1280,
        frame_height: int = 720
    ):
        """
        Initialize the oven video mixer.
        
        Args:
            video_paths: List of 4 video file paths [low, medium, high, overdrive]
            frame_width: Output frame width
            frame_height: Output frame height
        """
        if len(video_paths) != 4:
            raise ValueError("Exactly 4 video paths required: [low, medium, high, overdrive]")
        
        self.frame_width = frame_width
        self.frame_height = frame_height
        self.video_names = ["low", "medium", "high", "overdrive"]
        self.black_frame = np.zeros((self.frame_height, self.frame_width, 3), dtype=np.uint8)
        
        # Load all videos into RAM in parallel
        print("Loading videos into RAM...")
        
        with multiprocessing.Pool(processes=3) as pool:
            results = pool.starmap(load_video_frames, [(path, self.frame_width, self.frame_height) for path in video_paths])
        
        self.frames = results
        for i, frames in enumerate(results):
            if not frames:
                raise RuntimeError(f"Failed to load video {i}: {video_paths[i]}")
            print(f"  Loaded {self.video_names[i]}: {len(frames)} frames")
        
        print(f"  Loaded {len(self.frames)} videos")
    
    def _get_frame(self, video_idx: int, frame_num: int) -> np.ndarray:
        """Get a looping frame from a video."""
        frames = self.frames[video_idx]
        looped_idx = frame_num % len(frames)
        return frames[looped_idx]
    
    def blend_frames(
        self,
        frame1: np.ndarray,
        frame2: np.ndarray,
        alpha: float
    ) -> np.ndarray:
        """
        Blend two frames together.
        
        Args:
            frame1: First frame
            frame2: Second frame
            alpha: Blend factor (0.0 = frame1, 1.0 = frame2)
        
        Returns:
            Blended frame
        """
        alpha = np.clip(alpha, 0.0, 1.0)
        return cv2.addWeighted(frame1, 1 - alpha, frame2, alpha, 0)
    
    def get_frame(self, frame_num: int, intensity: float) -> np.ndarray:
        """
        Get a frame based on oven intensity (0-1).
        
        Intensity mapping:
        - 0.00: black screen
        - 0.00-0.25: blend between black and low
        - 0.25-0.50: blend between low and medium
        - 0.50-0.75: blend between medium and high
        - 0.75-1.00: blend between high and overdrive
        
        Args:
            frame_num: Current frame number
            intensity: Oven intensity (0.0 to 1.0)
        
        Returns:
            Blended frame
        """
        intensity = np.clip(intensity, 0.0, 1.0)
        
        if intensity <= 0.0:
            return self.black_frame.copy()

        if intensity < 0.25:
            # Blend between black and low (0)
            blend_alpha = intensity / 0.25
            frame1 = self.black_frame
            frame2 = self._get_frame(0, frame_num)

        elif intensity < 0.5:
            # Blend between low (0) and medium (1)
            blend_alpha = (intensity - 0.25) / 0.25
            frame1 = self._get_frame(0, frame_num)
            frame2 = self._get_frame(1, frame_num)

        elif intensity < 0.75:
            # Blend between medium (1) and high (2)
            blend_alpha = (intensity - 0.5) / 0.25
            frame1 = self._get_frame(1, frame_num)
            frame2 = self._get_frame(2, frame_num)

        else:
            # Blend between high (2) and overdrive (3)
            blend_alpha = (intensity - 0.75) / 0.25
            frame1 = self._get_frame(2, frame_num)
            frame2 = self._get_frame(3, frame_num)
        
        return self.blend_frames(frame1, frame2, blend_alpha)


def main():
    """Real-time oven video mixer with intensity control."""
    
    video_paths = [
        "/home/energiby/repositories/energiby-yderzonen/oven_low.mp4",
        "/home/energiby/repositories/energiby-yderzonen/oven_medium.mp4",
        "/home/energiby/repositories/energiby-yderzonen/oven_high.mp4",
        "/home/energiby/repositories/energiby-yderzonen/oven_overdrive.mp4",
    ]
    
    # Check if videos exist
    missing_videos = [p for p in video_paths if not Path(p).exists()]
    if missing_videos:
        print(f"Error: Missing video files: {missing_videos}")
        return
    
    # Get screen resolution for fullscreen scaling
    root = tk.Tk()
    screen_width = root.winfo_screenwidth()
    screen_height = root.winfo_screenheight()
    root.destroy()
    
    # Create mixer
    mixer = OvenVideoMixer(
        video_paths=video_paths,
        frame_width=screen_width,
        frame_height=screen_height
    )
    
    print("Starting video playback...")
    print("Controls:")
    print("  W/S: Increase/Decrease intensity")
    print("  'T': Test mode (auto-cycle through intensities)")
    print("  'M': Toggle manual/OSC control")
    print("  'Q' or ESC: Quit")
    print()
    
    # OSC control setup
    osc_intensity = {'value': 0.5, 'manual_mode': False}
    
    def osc_intensity_handler(addr, value):
        """Handle incoming OSC intensity messages."""
        if not osc_intensity['manual_mode']:
            osc_intensity['value'] = max(0.0, min(1.0, value))
    
    # Setup OSC server
    osc_dispatcher = dispatcher.Dispatcher()
    parser = argparse.ArgumentParser()
    parser.add_argument("--ip", default="0.0.0.0", help="The ip to listen on")
    parser.add_argument("--port", type=int, default=7134, help="The port to listen on")
    args = parser.parse_args()
    
    osc_dispatcher.map("/OvenIntensity", osc_intensity_handler)
    
    server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), osc_dispatcher)
    print(f"OSC Server listening on {server.server_address}")
    
    # Start OSC in a thread
    osc_thread = Thread(target=server.serve_forever)
    osc_thread.daemon = True
    osc_thread.start()
    
    frame_num = 0
    intensity = 0.5
    test_mode = False
    test_speed = 0.01
    prev_time = time.time()
    last_fps = 30.0
    cursor_hider_process = None
    
    # Create fullscreen window
    cv2.namedWindow("Oven Video Mixer", cv2.WINDOW_NORMAL)
    cv2.setWindowProperty("Oven Video Mixer", cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    # Hide cursor while fullscreen playback is active (Linux/X11).
    if shutil.which("unclutter"):
        cursor_hider_process = subprocess.Popen(
            ["unclutter", "-idle", "0", "-root"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    else:
        print("Warning: 'unclutter' not found, mouse cursor will remain visible")
    
    while True:
        loop_start = time.time()
        
        # Use OSC intensity if not in manual mode
        if not osc_intensity['manual_mode'] and not test_mode:
            intensity = osc_intensity['value']
        
        # Get current frame
        frame = mixer.get_frame(frame_num, intensity)
        
        # Add UI overlay
        h, w = frame.shape[:2]
        overlay = frame.copy()
        
        # Draw intensity bar
        bar_height = 40
        bar_y = h - bar_height - 10
        bar_width = int(w * 0.8)
        bar_x = (w - bar_width) // 2
        
        # Background
        cv2.rectangle(overlay, (bar_x, bar_y), (bar_x + bar_width, bar_y + bar_height), (0, 0, 0), -1)
        
        # Fill based on intensity
        fill_width = int(bar_width * intensity)
        cv2.rectangle(overlay, (bar_x, bar_y), (bar_x + fill_width, bar_y + bar_height), (0, 200, 255), -1)
        
        # Border
        cv2.rectangle(overlay, (bar_x, bar_y), (bar_x + bar_width, bar_y + bar_height), (255, 255, 255), 2)
        
        # Blend overlay
        frame = cv2.addWeighted(frame, 0.7, overlay, 0.3, 0)
        
        # Add text
        mode_text = "MANUAL" if osc_intensity['manual_mode'] else "OSC"
        text = f"Intensity: {intensity:.2f} FPS: {last_fps:.1f} [{mode_text}]"
        if test_mode:
            text += " [TEST MODE]"
        cv2.putText(
            frame,
            text,
            (20, 40),
            cv2.FONT_HERSHEY_SIMPLEX,
            1.2,
            (0, 255, 0),
            2
        )
        
        # Display
        cv2.imshow("Oven Video Mixer", frame)
        
        # Calculate FPS
        current_time = time.time()
        fps = 1 / (current_time - prev_time) if (current_time - prev_time) > 0 else 30.0
        prev_time = current_time
        last_fps = fps
        
        # Adjust wait time to maintain 30 fps
        elapsed = time.time() - loop_start
        target_fps = 30.0
        target_time = 1 / target_fps
        wait_ms = max(1, int((target_time - elapsed) * 1000))
        
        # Handle input
        key = cv2.waitKey(wait_ms) & 0xFF
        
        if key == ord('q') or key == 27:  # Q or ESC
            break
        elif key == ord('w'):  # W
            intensity = min(1.0, intensity + 0.05)
            osc_intensity['manual_mode'] = True
        elif key == ord('s'):  # S
            intensity = max(0.0, intensity - 0.05)
            osc_intensity['manual_mode'] = True
        elif key == ord('t'):  # Test mode
            test_mode = not test_mode
            osc_intensity['manual_mode'] = True
        elif key == ord('m'):  # Toggle manual/OSC mode
            osc_intensity['manual_mode'] = not osc_intensity['manual_mode']
            if not osc_intensity['manual_mode']:
                print("Switched to OSC control mode")
            else:
                print("Switched to manual control mode")
        
        # Auto-cycle in test mode
        if test_mode:
            intensity += test_speed
            if intensity >= 1.0 or intensity <= 0.0:
                test_speed *= -1
        
        frame_num += 1
        frame_num %= max(len(mixer.frames[0]), len(mixer.frames[1]), len(mixer.frames[2]), len(mixer.frames[3]))
    
    cv2.destroyAllWindows()
    if cursor_hider_process:
        cursor_hider_process.terminate()
        try:
            cursor_hider_process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            cursor_hider_process.kill()
    print("Done!")


if __name__ == "__main__":
    main()
