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
        
        # Load all videos into RAM
        print("Loading videos into RAM...")
        self.frames = []
        
        for i, path in enumerate(video_paths):
            print(f"  Loading {self.video_names[i]}: {path}")
            frames = self._load_video(path)
            if not frames:
                raise RuntimeError(f"Failed to load video {i}: {path}")
            self.frames.append(frames)
        
        print(f"  Loaded {len(self.frames)} videos")
    
    def _load_video(self, path: str) -> Optional[List[np.ndarray]]:
        """Load all frames from a video file into RAM."""
        cap = cv2.VideoCapture(str(path))
        if not cap.isOpened():
            return None
        
        frames = []
        while True:
            ret, frame = cap.read()
            if not ret:
                break
            
            # Resize to output dimensions
            frame = cv2.resize(frame, (self.frame_width, self.frame_height))
            frames.append(frame)
        
        cap.release()
        return frames if frames else None
    
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
        - 0.00-0.33: blend between low and medium
        - 0.33-0.66: blend between medium and high
        - 0.66-1.00: blend between high and overdrive
        
        Args:
            frame_num: Current frame number
            intensity: Oven intensity (0.0 to 1.0)
        
        Returns:
            Blended frame
        """
        intensity = np.clip(intensity, 0.0, 1.0)
        
        if intensity < 0.33:
            # Blend between low (0) and medium (1)
            blend_alpha = intensity / 0.33
            frame1 = self._get_frame(0, frame_num)
            frame2 = self._get_frame(1, frame_num)
        
        elif intensity < 0.66:
            # Blend between medium (1) and high (2)
            blend_alpha = (intensity - 0.33) / 0.33
            frame1 = self._get_frame(1, frame_num)
            frame2 = self._get_frame(2, frame_num)
        
        else:
            # Blend between high (2) and overdrive (3)
            blend_alpha = (intensity - 0.66) / 0.34
            frame1 = self._get_frame(2, frame_num)
            frame2 = self._get_frame(3, frame_num)
        
        return self.blend_frames(frame1, frame2, blend_alpha)


def main():
    """Real-time oven video mixer with intensity control."""
    
    video_paths = [
        "oven_low.mp4",
        "oven_medium.mp4",
        "oven_high.mp4",
        "oven_overdrive.mp4",
    ]
    
    # Check if videos exist
    missing_videos = [p for p in video_paths if not Path(p).exists()]
    if missing_videos:
        print(f"Error: Missing video files: {missing_videos}")
        return
    
    # Create mixer
    mixer = OvenVideoMixer(
        video_paths=video_paths,
        frame_width=720,
        frame_height=1280
    )
    
    print("Starting video playback...")
    print("Controls:")
    print("  Q/S: Increase/Decrease intensity")
    print("  'T': Test mode (auto-cycle through intensities)")
    print("  'Q' or ESC: Quit")
    print()
    
    frame_num = 0
    intensity = 0.5
    test_mode = False
    test_speed = 0.01
    
    # Create fullscreen window
    cv2.namedWindow("Oven Video Mixer", cv2.WINDOW_FULLSCREEN)
    
    while True:
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
        text = f"Intensity: {intensity:.2f}"
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
        
        # Handle input
        key = cv2.waitKey(30) & 0xFF
        
        if key == ord('q') or key == 27:  # Q or ESC
            break
        elif key == ord('w'):  # W
            intensity = min(1.0, intensity + 0.05)
        elif key == ord('s'):  # S
            intensity = max(0.0, intensity - 0.05)
        elif key == ord('t'):  # Test mode
            test_mode = not test_mode
        
        # Auto-cycle in test mode
        if test_mode:
            intensity += test_speed
            if intensity >= 1.0 or intensity <= 0.0:
                test_speed *= -1
        
        frame_num += 1
        frame_num %= max(len(mixer.frames[0]), len(mixer.frames[1]), len(mixer.frames[2]), len(mixer.frames[3]))
    
    cv2.destroyAllWindows()
    print("Done!")


if __name__ == "__main__":
    main()
