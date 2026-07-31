# data_collection_aruco

ArUcoでゴール判定しながら、画像・エンコーダ値・実行コマンドを保存するデータ収集プロジェクトです。

## コマンド

- `1`〜`9`: 姿勢変更
- `g`: 現在姿勢で前進
- `b`: エンコーダ基準値を更新
- `x`: 終了

`s` は廃止しています。停止はArUcoの閾値判定で行います。

## ゴール判定

`aruco_detector.cpp / hpp` の初期値では、以下を満たしたら終了します。

- `Z <= 300 mm`
- `|angle| <= 5 deg`
- 対象マーカID: `0`
- マーカ長: `0.10 m`
- カメラパラメータ: `/home/ubuntu/yaml/camera_many.yaml`

変更したい場合は `ArucoDetector` のコンストラクタ初期値を変更してください。

## ビルド

```bash
make clean
make
```

## 実行

```bash
sudo ./main
```

起動後に実験名とモードを入力します。

```txt
Experiment name: left_start_001
Mode (manual/auto): manual
```

## manualモード

1コマンドごとに、ArUco判定を先に行います。
ゴール条件を満たしていなければ、入力したコマンドに対応して画像・エンコーダ値・ArUco値を保存してから動作します。
実行したコマンドは `data/<run_name>/commands.txt` に保存されます。

## autoモード

manualモードで作った `commands.txt` などを指定すると、そのコマンド列を上から順番に実行します。
各コマンドの前にArUco判定を行い、ゴール条件を満たしたらコマンド列の途中でも終了します。

例:

```txt
Experiment name: left_start_replay_001
Mode (manual/auto): auto
Command txt path: data/left_start_001_manual_YYYYMMDD_HHMMSS_mmm/commands.txt
```

## 保存されるもの

```txt
data/<run_name>/
  log.csv
  commands.txt
  images/
```

`log.csv` には、画像パス、コマンド、エンコーダraw/diff、ArUcoの検出結果、Z距離、角度が保存されます。
