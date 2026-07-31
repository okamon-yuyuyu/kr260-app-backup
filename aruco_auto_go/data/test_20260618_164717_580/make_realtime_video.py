import csv
from datetime import datetime
import subprocess
import os

csv_path = "log.csv"
list_path = "frames.txt"
out_path = "aruco_run_realtime.mp4"

def parse_time(s):
    return datetime.strptime(s, "%Y%m%d_%H%M%S_%f")

def fix_image_path(image_path):
    # log.csv の image_path が
    # data/test_xxx/images/xxx.jpg になっている場合、
    # 実験フォルダ内から見た相対パス images/xxx.jpg に直す
    if image_path.startswith("data/"):
        return image_path.split("/", 2)[2]
    return image_path

rows = []

with open(csv_path, newline="") as f:
    reader = csv.DictReader(f)
    for row in reader:
        rows.append(row)

if len(rows) < 2:
    print("log.csv の行数が少なすぎます")
    exit(1)

with open(list_path, "w") as f:
    for i in range(len(rows) - 1):
        t0 = parse_time(rows[i]["timestamp"])
        t1 = parse_time(rows[i + 1]["timestamp"])
        duration = (t1 - t0).total_seconds()

        image_path = fix_image_path(rows[i]["image_path"])

        if not os.path.exists(image_path):
            print("画像が見つかりません:", image_path)
            continue

        f.write(f"file '{image_path}'\n")
        f.write(f"duration {duration:.3f}\n")

    # 最後のフレームはもう一度書く必要あり
    last_image_path = fix_image_path(rows[-1]["image_path"])

    if os.path.exists(last_image_path):
        f.write(f"file '{last_image_path}'\n")
    else:
        print("最後の画像が見つかりません:", last_image_path)

subprocess.run([
    "ffmpeg",
    "-y",
    "-f", "concat",
    "-safe", "0",
    "-i", list_path,
    "-vcodec", "libx264",
    "-pix_fmt", "yuv420p",
    out_path
])
