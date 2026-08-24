# peri_AXI_predict_torchscript_safe

Crank P1の学習済みCNN/ResNet18を、KR260上のC++とLibTorch CPUで推論し、既存のAXI走行制御へ接続する安全機能追加版アプリです。元の`peri_AXI_predict_torchscript`は変更せず残します。

カメラ、90度回転、ArUco、AXIコマンド、エンコーダ、dead band、ワイヤ初期調整、終了時姿勢15復帰は`peri_AXI_collect`のC++実装を引き継いでいます。Pythonプロセスは実機走行中に使用しません。

## 今回のPoint1CNN

- 畳み込み`1→32→64→128→256`
- `AdaptiveAvgPool2d((4, 4))`
- 全結合`4096→256→5`
- GRAY、1チャンネル
- 高さ320×幅180
- 5クラス`[4, 5, 6, 7, 100]`
- 画素値を0〜1へ変換、追加正規化なし
- 導入対象はepoch 70の最終重みではなく、validation macro F1が最大だったepoch 55の`best_model.pth`

## ファイル構成

- `pc_tools/export_pth_to_torchscript.py`：PCで`.pth`を`.pt + YAML`へ変換
- `torchscript_predictor.*`：LibTorchモデル読込み、前処理、推論
- `main.cpp`：カメラ、ArUco、走行制御、ログ
- `benchmark_torchscript.cpp`：保存画像による速度測定
- `emergency_monitor.*`：auto推論中のEnter不要な`e`キー監視
- `safety_logger.*`：停止した推論コマンド番号と停止時状態の記録

## 1. PCでbest `.pth`を変換

学習に使った`kr260-ai`環境で実行します。CNNには追加パッケージは不要です。

```powershell
python export_pth_to_torchscript.py --pth "C:\path\to\best_model.pth"
```

checkpointに画像サイズなどのメタデータがない場合だけ明示します。

```powershell
python export_pth_to_torchscript.py `
  --pth "C:\path\to\best_model.pth" `
  --architecture cnn `
  --height 320 `
  --width 180 `
  --image-mode GRAY `
  --commands 4 5 6 7 100
```

次の2ファイルが生成されます。

- `best_model_torchscript.pt`
- `best_model_torchscript.yaml`

変換処理は、元のPyTorchモデルと保存後のTorchScriptモデルを、0〜1の固定乱数入力3件で比較します。許容誤差内で`outputs close: True`かつ`argmax match: True`にならないモデルはKR260へ持ち込みません。

PCがx86、KR260がArmなので、CPU固有最適化を焼き込む`optimize_for_inference`や、BatchNormをConvへ畳み込む`freeze`はPCでは実行していません。通常のtraceと出力一致確認を行います。

## 2. KR260へ配置

```powershell
ssh ubuntu@kria "mkdir -p /home/ubuntu/models /home/ubuntu/app"
scp cnn02_ts60_ss42_tr1001_best_model_torchscript.pt ubuntu@kria:/home/ubuntu/models/
scp cnn02_ts60_ss42_tr1001_best_model_torchscript.yaml ubuntu@kria:/home/ubuntu/models/
scp -r peri_AXI_predict_torchscript_safe ubuntu@kria:/home/ubuntu/app/
```

## 3. KR260のLibTorch開発環境を確認

Python版PyTorchと、同梱されているC++ヘッダ・共有ライブラリを使用します。

```bash
python3 -c "import pathlib, torch; p=pathlib.Path(torch.__file__).resolve().parent; print(torch.__version__); print(p); print((p/'include').exists(), (p/'lib').exists()); print(torch._C._GLIBCXX_USE_CXX11_ABI)"
pkg-config --modversion opencv4
```

`include`または`lib`が`False`の場合、そのPyTorchパッケージだけではC++をビルドできません。

PyTorchのC++ ABIが`0`でsystem OpenCVがABI `1`の場合、リンク時に`std::__cxx11`を含む未定義参照が出る可能性があります。その場合はコードの問題ではなくライブラリのABI不一致なので、エラー全文を確認してからPyTorch/OpenCVの組合せを揃えます。Makefileは少なくともPyTorch側のABI値を自動取得して表示します。

## 4. ビルド

Torchヘッダのコンパイルはメモリを使うため、最初は並列化しません。

```bash
cd /home/ubuntu/app/peri_AXI_predict_torchscript_safe
make check-env
make clean
make -j1
```

Makefileは、`python3`からPyTorchのinclude/libパスとC++ ABI設定を自動取得します。

## 5. まず保存画像で速度測定

画像はデータ収集時に90度回転済みのJPEGを使います。warm-up 10回の後、同じ画像を100回推論します。

```bash
./benchmark_torchscript \
  --model /home/ubuntu/models/cnn02_ts60_ss42_tr1001_best_model_torchscript.pt \
  --config /home/ubuntu/models/cnn02_ts60_ss42_tr1001_best_model_torchscript.yaml \
  --image /home/ubuntu/test_images/sample.jpg \
  --warmup 10 \
  --runs 100 \
  --torch-threads 4 \
  --output benchmark_torchscript.csv
```

比較の中心は`inference_ms`です。`total_ms`にはC++のリサイズ、GRAY化、Tensor化、softmaxも含まれます。

## 6. プレビュー

`--preview`では`/dev/mem`、ワイヤ調整、モータ指令を使用しません。

```bash
./peri_AXI_predict_torchscript_safe \
  --preview \
  --model /home/ubuntu/models/cnn02_ts60_ss42_tr1001_best_model_torchscript.pt \
  --config /home/ubuntu/models/cnn02_ts60_ss42_tr1001_best_model_torchscript.yaml \
  --experiment Crank_P1_CNN02_preview \
  --max-steps 30 \
  --min-confidence 0 \
  --torch-threads 4
```

確認項目：

- `architecture: cnn`、`input: 1x320x180`と表示される
- コマンド順が`[4, 5, 6, 7, 100]`
- PCとKR260で同じ保存画像の予測コマンドが一致する
- `prediction_log.csv`に前処理時間、推論時間、合計時間、全クラス確率が保存される

## 7. 実機走行

```bash
sudo ./peri_AXI_predict_torchscript_safe \
  --model /home/ubuntu/models/cnn02_ts60_ss42_tr1001_best_model_torchscript.pt \
  --config /home/ubuntu/models/cnn02_ts60_ss42_tr1001_best_model_torchscript.yaml \
  --experiment Crank_P1_CNN02_run01 \
  --max-steps 30 \
  --min-confidence 0 \
  --torch-threads 4
```

実行順：

1. TorchScriptとYAMLを読み込む
2. warm-upを5回実行する
3. 開始時バッテリー電圧を入力する
4. dead bandをmanual=5、auto=20へ設定する
5. 12bit目標値`[725, 3453, 2700, 267]`でワイヤを調整する
6. 姿勢5と安全状態を確認し、実機推論直前に`y`を入力する
7. 撮影→ArUco→CNN推論→AXI送信→done待ちを繰り返す
8. 接触時は`e`をEnterなしで押すか、Ctrl+Cを入力する
9. 終了時に姿勢15（操作上の姿勢5）へ戻す

P1専用モデルはcmd900を学習していないため、ゴール停止はArUcoが担当します。

## 安全停止と記録

実機推論中は`e`をEnterなしで押すと停止要求を出せます。Ctrl+CとSIGTERMもプロセスを即終了させず、同じ安全終了処理へ移します。

停止要求を検出する場所は次のとおりです。

- 撮影と推論の間
- 推論完了後からAXI送信直前まで
- PLコマンドのdone待ち中
- コマンド間

停止要求後は新しいAXIコマンドを送りません。すでにPLで実行中のコマンドがある場合は、その完了を最大10秒待ってから姿勢5へ戻します。

`prediction_log.csv`には、状況に応じて次のstatusを記録します。

- `emergency_stop_before_send`
- `emergency_stop_during_command`
- `command_timeout`

同じ実験フォルダに`emergency_stop.csv`を追加し、次を保存します。

- 1始まりの`command_no`
- 停止時点で最後まで完了していたコマンド番号
- 推論コマンドとconfidence
- 停止要求を検出したphase
- 停止時の画像、ArUco、エンコーダ値
- 実行中PLコマンドの完了結果
- 姿勢5への復帰結果

停止がなかった実験では、`emergency_stop.csv`はヘッダーのみになります。

## 停止条件

- ArUcoが`Z <= 300 mm`かつ`|angle| <= 15°`
- 設定した最低確信度を下回る
- 未対応コマンドを予測
- PLのdoneが10秒以内に立たない
- `--max-steps`到達
- `e`キー、Ctrl+CまたはSIGTERM

この機能はPS側のソフト停止です。停止要求後に次のコマンドは送りませんが、すでにPLで実行中のストロークを途中で強制終了する機能ではありません。実行中ストロークを即時に止めるには、PL側にabort信号やmotor-enable信号を追加する必要があります。

最初は確信度閾値を未設定の`0`にしてPC/KR260の一致を確認し、プレビューログを見てから閾値を決めます。
