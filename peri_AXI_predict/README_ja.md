# peri_AXI_predict

`peri_AXI_collect` のカメラ、ArUco、AXIコマンド、エンコーダ、ワイヤ初期調整を引き継ぎ、Crank P1の学習済みモデルで実機推論するアプリです。

## 結論：KR260では `.pth` を直接読まない

推奨する流れは次のとおりです。

1. PCで学習済み `.pth` を読む。
2. PCで `モデル.onnx` と `モデル.yaml` を生成する。
3. 2ファイルをKR260へコピーする。
4. `peri_AXI_predict` がONNXをOpenCV DNNで読み、推論結果をAXIコマンドへ変換する。

`.pth` は重みとメタデータであり、同じPyTorchモデル定義がなければ読み込めません。一方、ONNXには計算グラフが含まれるため、KR260側はNN/CNN/ResNet18のC++クラスを切り替える必要がありません。モデル変更時はONNXとYAMLのパスを変えるだけです。

## 現在の対応範囲

- CNN v1/v2/v3：添付された `Crank_P1_CNN(20260818-055105).py` と同じ層構成を実装済み。
- ResNet18：torchvision ResNet18、320×180、ImageNet正規化に対応。
- 任意のNN：学習コード内に存在する正確な `model` インスタンスから共通関数で書き出せる。
- `.pth` だけから任意のNNを復元：NNの正確な層構成が必要。層構成を推測して重みを当てる処理は入れていない。

コマンド順はチェックポイントの `cmd_to_label` または `label_to_cmd` から読みます。今回の標準値は `[3, 4, 5, 6, 7, 100, 900]` です。

## 1. PCでCNNまたはResNet18を変換

現在の `kr260-ai` 環境で、追加パッケージを入れます。

```powershell
pip install onnx opencv-python
```

その後、次を実行します。

```powershell
python export_pth_to_onnx.py --pth "C:\path\to\best_model.pth"
```

チェックポイント内の `model_type`、画像サイズ、GRAY/RGB、クラス対応を原則として自動使用します。同じフォルダに次の2ファイルができます。

- `best_model.onnx`
- `best_model.yaml`

変換時にはONNXの構文検査と、PyTorch/OpenCV DNNの出力一致検査を行います。ここでエラーが出たモデルは実機へ持っていかないでください。

### CNNの明示指定

```powershell
python export_pth_to_onnx.py --pth "C:\path\to\best_model.pth" --architecture cnn
```

### ResNet18の明示指定

```powershell
python export_pth_to_onnx.py --pth "C:\path\to\best_model.pth" --architecture resnet18
```

## 2. 任意のNNを変換

任意のNNでは、学習スクリプトのテスト・結果保存がすべて終わり、ベストモデルが `model` に入っている箇所へ次を追加します。`model.cpu()` を呼ぶため、その後にGPUで評価を続ける位置へは入れないでください。これは層構成を推測せず、実際に学習へ使ったモデルそのものをONNX化します。

```python
from export_pth_to_onnx import export_loaded_model

export_loaded_model(
    model=model.cpu(),
    checkpoint=checkpoint,
    onnx_path=RESULT_DIR / f"{FILE_PREFIX}_best_model.onnx",
    yaml_path=RESULT_DIR / f"{FILE_PREFIX}_best_model.yaml",
    # checkpointに次の値が保存されていれば、以下の指定は不要です。
    image_height=IMG_HEIGHT,
    image_width=IMG_WIDTH,
    image_mode=IMAGE_MODE,
    input_channels=INPUT_CHANNELS,
    model_type=MODEL_NAME,
    commands=[3, 4, 5, 6, 7, 100, 900],
    normalization="none",
)
```

`export_pth_to_onnx.py` を学習スクリプトと同じフォルダへ置いて実行します。ResNet18の場合だけ `normalization="imagenet"` を指定します。

## 3. KR260へ配置

例として、PCのPowerShellから次のようにコピーします。

```powershell
scp best_model.onnx ubuntu@kria:/home/ubuntu/models/
scp best_model.yaml ubuntu@kria:/home/ubuntu/models/
scp -r peri_AXI_predict ubuntu@kria:/home/ubuntu/app/
```

## 4. KR260でビルド

```bash
cd /home/ubuntu/app/peri_AXI_predict
pkg-config --modversion opencv4
make
```

`pkg-config` がOpenCVを見つけない場合は、KR260の環境に `libopencv-dev` が必要です。

## 5. まずプレビューモードで確認

プレビューは `/dev/mem`、ワイヤ調整、モータ指令を使いません。カメラ、ArUco、推論、画像保存、CSV保存だけを確認します。

```bash
./peri_AXI_predict \
  --preview \
  --model /home/ubuntu/models/best_model.onnx \
  --config /home/ubuntu/models/best_model.yaml \
  --experiment Crank_P1_preview \
  --max-steps 30
```

確認項目：

- 入力が `1x320x180`（GRAY CNN）または `3x320x180`（ResNet18/RGB）と表示される。
- 実際の画像に対して `cmd3`〜`cmd900` の確率が表示される。
- `data/.../images` と `prediction_log.csv` が作られる。
- PCで同じ画像を推論した結果と、KR260の予測クラスが一致する。

## 6. 実機走行

```bash
sudo ./peri_AXI_predict \
  --model /home/ubuntu/models/best_model.onnx \
  --config /home/ubuntu/models/best_model.yaml \
  --experiment Crank_P1_run01 \
  --min-confidence 0.60 \
  --max-steps 200
```

実行順：

1. ONNXとYAMLを検証して読み込む。
2. 開始時バッテリー電圧を入力する。
3. dead bandを manual=5、auto=20 に設定する。
4. 12bit目標値 `[725, 3453, 2700, 267]` でワイヤを調整する。
5. `y` を入力して自律推論を始める。
6. 各画像について推論し、姿勢コマンドまたは `cmd100` を送る。
7. 終了時は姿勢15へ戻す。

## 停止条件

- ArUcoが `Z <= 300 mm` かつ `|angle| <= 15°`。
- モデルが `cmd900` を予測。
- 最大確率が `--min-confidence` 未満。
- モデルが未対応コマンドを出力。
- PLのcommand doneが10秒以内に立たない。
- `--max-steps` に到達。
- Ctrl+CまたはSIGTERM。

通常モードでは停止後に姿勢15への復帰指令を送ります。低確信度時は新しい走行指令を送らず停止します。

## 保存内容

各実行は `data/<実験名>_predict_<日時>/` に保存されます。

- `images/`：推論直前の画像。ファイル名に予測コマンドと確信度を含む。
- `prediction_log.csv`：全クラス確率、推論時間、送信状態、姿勢、開始時電圧、16bit/12bitエンコーダ、差分、ArUco姿勢。

## 前処理の一致

添付CNNに合わせ、CNNでは次を行います。

- C++カメラで90°時計回りに回転。
- 320×180へリサイズ（高さ320、幅180）。
- GRAYなら1チャンネル化。
- 画素値を0〜1へ変換。
- CNNでは平均・標準偏差による追加正規化なし。

ResNet18では3チャンネル入力とImageNetの mean/std を使います。YAMLに前処理情報を固定しているため、モデルを変えたときに実機コード側の定数を手作業で変更する必要はありません。
