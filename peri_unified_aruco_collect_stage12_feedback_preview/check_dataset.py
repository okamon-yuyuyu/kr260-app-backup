#!/usr/bin/env python3
"""Check Stage11 image/label quality and restored Github peri_AXI_collect_safe fields."""
import collections
import csv
from pathlib import Path
import statistics
import sys

if len(sys.argv) != 2:
    print('Usage: python3 check_dataset.py sessions/YYYYmmdd_...', file=sys.stderr)
    sys.exit(2)
root = Path(sys.argv[1])
with (root / 'log.csv').open(newline='', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))
with (root / 'legacy_log.csv').open(newline='', encoding='utf-8') as f:
    legacy = list(csv.DictReader(f))
missing_columns = [name for prefix, n in [('raw', 4), ('raw12_', 4), ('diff', 4), ('base', 4)]
                   for name in (prefix + str(i) for i in range(n)) if name not in (rows[0] if rows else {})]
if missing_columns:
    print('ERROR: missing encoder columns:', missing_columns, file=sys.stderr)
    sys.exit(1)
print('Events:', len(rows))
print('Kinds:', dict(collections.Counter(r['event_type'] for r in rows)))
print('Flags:', dict(collections.Counter(r['quality'] for r in rows)))
print('ArUco detections:', sum(r['aruco_detected'] == '1' for r in rows), '/', len(rows))
missing = [r['image_path'] for r in rows if r['image_path'] and not (root / r['image_path']).is_file()]
failed = [r['image_path'] for r in rows if r['jpeg_status'] == 'FAILED']
pending = [r['image_path'] for r in rows if r['jpeg_status'] == 'PENDING']
sampled = [r for r in rows if r['event_type'] in ('ACTION', 'GOAL')]
missing_encoder = [r['event_no'] for r in sampled if any(not r[f'{prefix}{i}']
                   for prefix in ('raw', 'raw12_', 'diff', 'base') for i in range(4))]
print('Missing JPEGs:', len(missing), missing[:5])
print('JPEG failed:', len(failed), failed[:5])
print('JPEG still pending in CSV:', len(pending))
print('Missing encoder on ACTION/GOAL:', len(missing_encoder), missing_encoder[:5])
print('Legacy rows:', len(legacy), 'action/goal rows:', len(sampled))
actions = [r for r in rows if r['event_type'] == 'ACTION']
good = [r for r in actions if r['quality'] == 'OK' and r['jpeg_status'] == 'SAVED'
        and r['image_path'] and (root / r['image_path']).is_file()
        and r['q'] and 0 <= int(r['q']) <= 100
        and r['event_no'] not in missing_encoder]
print('Valid action candidates:', len(good), '/', len(actions))
print('q frequency:', dict(sorted(collections.Counter(int(r['q']) for r in good).items())))
for key, label in [('camera_duration_ms', 'Capture'), ('aruco_duration_ms', 'ArUco'),
                   ('queue_wait_ms', 'Enqueue/deep-copy'), ('jpeg_ms', 'JPEG write (worker)')]:
    samples = [float(r[key]) for r in rows if r.get(key) and float(r[key]) >= 0]
    if samples:
        print(f'{label}: median {statistics.median(samples):.1f} ms / max {max(samples):.1f} ms')
if missing or failed or pending or missing_encoder or len(sampled) != len(legacy):
    sys.exit(1)

