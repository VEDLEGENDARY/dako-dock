"""Extract local reference frames for design review; no media uploads."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'build' / 'video-tools'))
import av
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
source = root / 'reference' / 'example.mp4'
output = root / 'build' / 'reference-frames'
output.mkdir(parents=True, exist_ok=True)
with av.open(str(source)) as container:
    stream = container.streams.video[0]
    seconds = float(stream.duration * stream.time_base) if stream.duration else container.duration / av.time_base
    print({'seconds': seconds, 'size': [stream.width, stream.height], 'fps': str(stream.average_rate)})
    times = [float(value) for value in sys.argv[1:]] if len(sys.argv) > 1 else [seconds * i / 15 for i in range(16)]
    canvas = Image.new('RGB', (4 * 480, ((len(times) + 3) // 4) * 300), '#181818')
    draw = ImageDraw.Draw(canvas)
    for index, timestamp in enumerate(times):
        container.seek(int(timestamp / stream.time_base), stream=stream, backward=True)
        for frame in container.decode(stream):
            if frame.time is not None and frame.time + 0.05 >= timestamp:
                picture = frame.to_image()
                picture.save(output / f'{timestamp:.2f}.png')
                picture.thumbnail((480, 270))
                x, y = (index % 4) * 480, (index // 4) * 300
                canvas.paste(picture, (x, y))
                draw.text((x + 8, y + 277), f'{timestamp:.2f}s', fill='white')
                break
    canvas.save(output / 'contact-sheet.jpg')
