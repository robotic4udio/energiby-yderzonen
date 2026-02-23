import vlc
import time

VIDEOS = [
    "oven_low.mp4",
    "oven_medium.mp4",
    "oven_high.mp4",
    "oven_overdrive.mp4"
]

instance = vlc.Instance(
    "--no-audio",
    "--fullscreen",
    "--vout=drm"
)

def create_player(video):
    player = instance.media_player_new()
    media = instance.media_new(video)
    player.set_media(media)
    player.play()
    time.sleep(0.2)
    return player

def set_alpha(player, value):
    player.video_set_adjust_int(vlc.VideoAdjustOption.Enable, 1)
    player.video_set_adjust_float(vlc.VideoAdjustOption.Alpha, value)

current_index = 0
current_player = create_player(VIDEOS[current_index])
next_player = None

intensity = 0.0

def update_intensity():
    # Replace with GPIO / sensor reading
    global intensity
    intensity += 0.002
    if intensity > 1:
        intensity = 0

while True:
    update_intensity()

    position = intensity * (len(VIDEOS) - 1)
    base = int(position)
    mix = position - base

    if base != current_index:
        if next_player:
            next_player.stop()

        next_player = create_player(VIDEOS[base])
        set_alpha(next_player, 0)
        current_index = base

    if next_player:
        set_alpha(current_player, 1 - mix)
        set_alpha(next_player, mix)

        if mix > 0.98:
            current_player.stop()
            current_player = next_player
            next_player = None

    time.sleep(0.02)