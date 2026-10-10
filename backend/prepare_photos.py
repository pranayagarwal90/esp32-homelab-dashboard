from pathlib import Path
from PIL import Image, ImageOps

import config

BASE_DIR = Path(__file__).resolve().parent
SOURCE_DIR = BASE_DIR / "photos-source"
# Where the API serves photos from (DASHBOARD_PHOTOS_DIR).
OUTPUT_DIR = config.load().photos_dir

TARGET_SIZE = (320, 240)
JPEG_QUALITY = 72
MAX_RECOMMENDED_BYTES = 120_000

SOURCE_DIR.mkdir(exist_ok=True)
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

extensions = {".jpg", ".jpeg", ".png", ".webp"}
files = sorted(
    p for p in SOURCE_DIR.iterdir()
    if p.is_file() and p.suffix.lower() in extensions
)

if not files:
    print(f"No source photos found in: {SOURCE_DIR}")
    print("Copy a few JPG/PNG/WebP photos there and run this script again.")
    raise SystemExit(0)

# Clear generated slideshow images only.
for old in OUTPUT_DIR.glob("photo_*.jpg"):
    old.unlink()

for index, source in enumerate(files, start=1):
    with Image.open(source) as image:
        image = ImageOps.exif_transpose(image).convert("RGB")
        image = ImageOps.fit(
            image,
            TARGET_SIZE,
            method=Image.Resampling.LANCZOS,
            centering=(0.5, 0.5),
        )

        output = OUTPUT_DIR / f"photo_{index:03d}.jpg"
        image.save(
            output,
            "JPEG",
            quality=JPEG_QUALITY,
            optimize=True,
            progressive=False,
        )

    size = output.stat().st_size
    warning = "  <-- large for ESP32" if size > MAX_RECOMMENDED_BYTES else ""
    print(f"{source.name} -> {output.name} ({size / 1024:.1f} KB){warning}")

print(f"\nPrepared {len(files)} photo(s) in {OUTPUT_DIR}")
