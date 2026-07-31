# aruco_auto_go

ArUcoマーカの `z_mm` と `angle_deg` を使って、閾値に入るまで `g`、入ったら `s` を送る自動前進停止プロジェクトです。

## 停止条件

```cpp
abs(angle_deg) <= 5.0 && z_mm <= 300.0
```

## ビルド

```bash
cd aruco_auto_go
make
```

## 実行

`/dev/mem` を使うので sudo が必要です。

```bash
sudo ./aruco_auto_go
```

## カメラキャリブレーションファイル

デフォルトでは以下を読みます。

```text
/home/ubuntu/yaml/camera_many.yaml
```

## 出力

実行すると以下に画像とCSVログを保存します。

```text
data/<experiment_name>_<timestamp>/images/
data/<experiment_name>_<timestamp>/log.csv
```
