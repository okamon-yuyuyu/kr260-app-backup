# Stage 12: Stage 11の全収集項目を維持し、AXI manual/auto選択を復元

**目的:** 以前の [peri_AXI_collect_safe](https://github.com/okamon-yuyuyu/kr260-app-backup/tree/main/peri_AXI_collect_safe) が集めていたデータを落とさず、AXI/PWM共通制御・REG5〜7撮影同期・ArUco検出・JPEG並列保存を維持する。**PLの変更は不要。**

## 復元・追加したファイル

- `encoder.cpp/.hpp`: 旧コードと同じ16bitのAXI ENCODER (`0xA0000000`) と12bitのエンコーダ (`0xA0030000`) を読み出す。各2レジスタから4chを取り出すビット配置は旧 `encoder.cpp` と同一。
- `logger.cpp/.hpp`: `log.csv` のStage10既存26列は**順番・名前を変更せず**、旧コード由来の `timestamp`, `command_id`, `command_name`, `rom0..3`, `stroke_right/left/up/down`, `stick1/3`, **`raw0..3`, `raw12_0..3`, `diff0..3`** を追加。さらに `base0..3` も追加した。
- `legacy_log.csv`: **旧GitHubの `logger.cpp` と完全に同じヘッダー・列順** (`index,timestamp,mode,start_battery_voltage_v,...,diff3`) で ACTION/GOAL を保存。旧版の列を使う解析に便利。指令値は現在のPL仕様へ合わせた値で、旧1〜9コマンドの数値を捏造していない。
- `commands.txt`: 実際にREG5で認識できた成功ステップの姿勢値(0〜100)を**1行1個**出力。PWMデータを `--commands sessions/.../commands.txt` でAXI固定列として再生できる。記録失敗時のデータは含めない。
- `emergency_stop.csv`: 旧GitHubと同じヘッダーで、Ctrl+C / 異常収集終了の際の理由とエンコーダ値を記録。**旧版と異なり、異常時の追加カメラ撮影は行わない**（そのため `image_path` とArUco座標は空欄、`aruco_detected=0`）。アクティブな指令の完了状況は保証できないため、完了フラグと `last_completed_command_no` は空欄。
- `check_dataset.py`: JPEG欠損だけでなく、ACTION/GOALに必要なエンコーダ12列と基準値4列が揃っていること、`legacy_log.csv` の件数を確認。
- `test_logger.cpp`, `test_safety_logger.cpp`: 列数、元のヘッダーとの完全一致、負のエンコーダ差分の保存をテスト。

## エンコーダの意味・撮影タイミング

- `raw0..raw3`: 16bitの生エンコーダカウンタ値（旧 `read_raw()`）。
- `raw12_0..raw12_3`: 12bitの生エンコーダ値（旧 `read_raw12()`）。
- `diff0..diff3`: **同一画像の取得直後、動作指令を出す前**の `raw - base`（旧 `read_diff()` と同じ演算）。
- `base0..base3`: 基準値。ワイヤ調整完了の`y`の直後に`set_base()`し、`session.txt`の`encoder_base16`にも記録する。
- AXI手動モードで`b`を入力した場合のみ、基準を再設定する（`BASE`イベントとして`log.csv`に記録）。その後の各ステップの`base0..3`も更新される。
- 16bitカウンタの周回をまたぐ場合の差分補正は**旧コードと同じく行わない**（単純な引き算）。
- エンコーダ値の取得は画像の読み出しが成功した直後、ArUco判定/REG6 ACK/AXI送信**より前**。JPEGファイル書き込みが後で完了しても、このエンコーダ値は撮影時点のまま残す。

## コマンド情報の扱い

- `command_id=100` は前進ステップ、`900` はGOAL、`999` は姿勢50復帰（最後のものは`log.csv`のみ）。
- 前進時の`stick3`はREG5が報告した実行姿勢、`stick1=50`は共通PLで未使用の中立値。GOALは`stick3`に最後に確認した姿勢を記録。
- `rom0..rom3`は現在のAXI ROMレジスタを読み出した値。`rom2/3`は通常100で、`rom0/1`はゼロとは限らないので読み戻し値を保存する。
- PWM前進の`stroke_right/left=2000`は**Stage5共通PLの固定値 `PWM_STEP_STROKE`**に基づく。AXI前進は設定画面で指定したストローク。GOALは次のコマンドを送らないためstroke=0。
- `commands.txt` は正常なステップだけを出力する。JPEG保存が後から失敗した時点では、既にコマンド列に追記されていることがあり得るため、**学習や再生前には必ず `check_dataset.py` を実行し、CSVの `jpeg_status` も確認**する。
- `quality=OK` / DONEはPLが機械的に指令を正常完了したことを保証しない（旧PLの3秒タイムアウトはdoneに統合されている）。

## 実行

```bash
unzip peri_unified_aruco_collect_stage11.zip
cd peri_unified_aruco_collect_stage11
make clean && make
make test
sudo ./peri_collect --name P1_trial01 --camera 0 --settle-ms 50 --flush-frames 2 --flush-sleep-ms 0
```

起動フローと操作方法はStage10と同じ。AXI/PWM選択→パラメータ変更(y/n)→電圧入力→リアルタイムのエンコーダ調整画面で`y`→開始。

- PWM: 連続スティック操作で「撮影→ArUco判定→REG6 ACK→姿勢採用・前進」を繰り返す。
- AXI: `q 30`, `g`, `b`（基準位置再設定）, `x`、または`--commands example_commands.txt`。
- GOAL: どちらも動画撮影終了まで姿勢を保持し、その後`y`で`q=50`に復帰、`n`で復帰をスキップ。
- JPEG: Stage10同様、`cv::Mat::clone()`でキュー投入後に保存スレッドへ。次のACK/コマンドを出す前に**ArUcoによるゴール判定は必ず完了**する。保存失敗判明後は次の動作を許可しないが、既に送った直前のACKは取り消せない。

出力:

```text
sessions/YYYYmmdd_HHMMSS_P1_trial01_pwm/
  images/*.jpg
  log.csv              # Stage10列 + 旧GitHubデータ列 + encoder baseline
  legacy_log.csv       # 旧GitHubの列順（action / goal）
  commands.txt         # 再生用のq列
  emergency_stop.csv   # 異常終了ログ（正常時はヘッダーのみ）
  session.txt          # 設定・バッテリー・encoder baseなど
```

チェック:

```bash
python3 check_dataset.py sessions/<実際のセッション名>
```

## 安全・テストの範囲

- PWM↔AXI切替時は右スティックを物理的に中立へ戻す。Ctrl+Cは**モータ停止ではない**。ハードウェア電源遮断を確保。
- PL変更なし。`REG5`は前進開始を通知し、前進正常終了を保証する信号ではない。
- C++変更箇所の構文検証、REG5〜7テスト、CSV互換性テスト、エンコーダ値／負の差分記録テスト、異常終了ログのテストを確認。OpenCVがないこの作業環境では一部をモックで検証した。**KR260上でのビルドとカメラ・モータ実機試験はまだ**。


## Stage12: AXIモード選択を復元（旧 peri_AXI_collect_safe に合わせる）

Stage11ではAXIの `manual/auto` 選択が欠落していたため復元した。**Stage11のJPEG非同期保存、全CSV項目、エンコーダ表示、ArUcoゴール判定、ゴール動画待機→yでq50復帰は維持**する。

通常の対話起動では、最初に `Mode (axi/pwm): axi` と入力すると、続けて次が表示される。

```text
Mode (axi/pwm): axi
Mode (manual/auto): auto
Command txt path: sessions/以前の実験/commands.txt
AXI AUTO replay file: 31 q values (0..100)
--- Unified KR260 dataset collection / axi ---
Current parameters: ...
Change parameters? (y/n): n
Battery voltage at start [V]: 11.7
Wire adjustment running... [yを入力]
...
Robot at safe initial posture, RIGHT stick NEUTRAL, and motor power cutoff ready. Start AXI AUTO replay? (y/n): y
```

`manual` を選んだ場合はコマンドファイル入力なしで、走行中に `q 30`, `g`, `b`, `x` を操作する。`auto` はファイルの **0〜100の姿勢値を1行ずつ**読み、ArUco撮影・ゴール判定後に順に送る。各行は `q 30` または `30` 形式。固定列の送信前に`y`による安全確認を追加した。

**CLIで自動実行する従来の方法は維持**：`--mode axi --commands example_commands.txt` が付いていると、`auto` を自動選択してモード入力/パス入力を省略する（開始直前の安全確認は残る）。`--mode axi --axi-control manual` または `--mode axi --axi-control auto --commands example_commands.txt` の指定も可能。PWMの起動順序は従来どおりで、manual/auto選択画面は出ない。

AXIのサブモードは`session.txt`の`axi_control=manual|auto`として残す。`log.csv`と`legacy_log.csv`の`mode`列は従来の `AXI` / `PWM` のまま。PL変更なし。

テスト：`make test_axi_launch && ./test_axi_launch` はホストPCでもOpenCVなしで実行可能。その他のテスト／実機ビルドはKR260上で`make clean && make && make test`で確認してほしい。Ctrl+Cはハードウェアのモータ停止指令ではないので非常停止手段を別に確保すること。

## Stage12 Feedback extension: ② PCオンラインフィードバック

この派生版はStage12を別フォルダへ複製して追加したもの。元の `--mode pwm` と `--mode axi`（manual/auto）は維持し、新しい `--mode feedback` だけを追加する。Feedback modeはAXIで動かし、PCがOverhead Open Cameraスクショから姿勢を計測し、選んだqをTCPでPSへ送る。

### 起動順

KR260:

```bash
sudo ./peri_collect --mode feedback --name P2_feedback_trial01 --feedback-port 5001
```

従来どおりパラメータ確認・ワイヤ調整を行い、最後の `READY` 確認後にTCP接続を待つ。接続後、PCは `HELLO 1 <現在q>` を送り、PSは `READY 1 <現在q>` を返す。PC側の初期qは実際のロボット姿勢と一致させる。

Windows PC（`11_feedback` の依存ファイルをそろえ、Open CameraとWi-Fi ADBを起動した状態）:

```powershell
py -3.14 online_screenshot_feedback_auto.py --host 192.168.10.140 --port 5001 --q 50
```

画面で `START` と入力すると、PCがスクショ計測・コマンド選択を行い、PSに1コマンドずつ送る。PSは各MOVEの前に前方カメラ画像とエンコーダを記録し、ArUcoゴール判定を行ってからAXI指令を出す。REG5で実行姿勢を確認し、PLのDONE信号を検出した後に限り、同じ連番の `DONE` をPCへ返す。PCはDONE後の静止待ちを経て次のスクショを取得する。

### TCP protocol v1

| Direction | Message | Meaning |
|---|---|---|
| PC → PS | `HELLO 1 q_current` | セッション開始と初期姿勢q |
| PS → PC | `READY 1 q_current` | PS側の準備完了 |
| PC → PS | `MOVE sequence q_current q_next` | 1周期分の姿勢＋蠕動を指令 |
| PS → PC | `DONE sequence q_next` | REG5の姿勢ラベルとPL DONE信号を確認 |
| PS → PC | `GOAL sequence q_current` | PS前方カメラでゴール検出。MOVEは未実行 |
| PC → PS | `GOAL sequence q_current` | 俯瞰スクショでGOAL_LINE到達を報告 |
| PS → PC | `GOAL_ACK sequence q_current` | ゴール報告を記録。以後動作なし |
| PC → PS | `STOP` | 新しい指令を止めてセッション終了 |
| PS → PC | `ERR sequence reason` | 指令不一致・撮影失敗・完了未確認などで停止 |

MOVEは連番が連続し、送信した `q_current` が直前の確認済みqと一致し、`q_next` が30〜70の範囲にある場合だけ受け付ける。コマンドを処理中は次のMOVEを読まない。通信が切れた場合も次の指令は受け付けないが、実行中の1周期を即時停止する機能ではない。

### 記録と制限

- 新規ファイル `feedback_commands.csv` に連番、q遷移、REG5の実行q、PL DONE信号、所要時間を記録する。既存の `log.csv`、`legacy_log.csv`、`commands.txt`、前方カメラJPEGも維持する。
- `session.txt` に `control_source=PC_ONLINE_FEEDBACK` とTCP設定を記録する。
- `DONE` はPLのDONE信号とREG5ラベルの確認を意味する。Stage12の既存仕様上、物理的に指令位置へ到達した保証ではない。
- PCが目標線に到達したと報告した場合、PSは前方画像を1枚保存してGOALとして記録し、追加の前進はしない。その後の動画保持・q=50復帰確認は既存のGOAL手順に従う。
- Ctrl+C、TCP切断、ソフトウェアのSTOPは非常停止ではない。実験時は物理電源遮断を使える状態にする。

この派生コードはリポジトリ上での追加実装であり、KR260へのビルド、PCとの通信、実機走行はまだ確認していない。

