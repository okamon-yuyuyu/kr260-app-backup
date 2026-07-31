import time
import torch
import torch.nn as nn


class SimpleNN(nn.Module):
    def __init__(self):
        super().__init__()

        self.fc = nn.Sequential(
            nn.Linear(28 * 28, 128),
            nn.ReLU(),

            nn.Linear(128, 64),
            nn.ReLU(),

            nn.Linear(64, 10)
        )

    def forward(self, x):
        x = x.view(x.size(0), 28 * 28)
        x = self.fc(x)
        return x


device = torch.device("cpu")
print("device:", device)
print("torch version:", torch.__version__)

model = SimpleNN().to(device)
model.load_state_dict(torch.load("mnist_nn.pth", map_location=device))
model.eval()

ref = torch.load("mnist_reference.pt", map_location=device)

x = ref["input"].to(device)
label = ref["label"]
out_host = ref["output_host"].to(device)

# 1回推論
with torch.no_grad():
    out_kr260 = model(x)

print("label:", label)
print("host output:")
print(out_host)
print("kr260 output:")
print(out_kr260)

print("host pred:", out_host.argmax(dim=1).item())
print("kr260 pred:", out_kr260.argmax(dim=1).item())

diff = torch.abs(out_host - out_kr260)
print("max abs error:", diff.max().item())
print("allclose:", torch.allclose(out_host, out_kr260, atol=1e-5, rtol=1e-4))

# 推論時間測定
num_runs = 100

with torch.no_grad():
    for _ in range(10):
        _ = model(x)

start_time = time.perf_counter()

with torch.no_grad():
    for _ in range(num_runs):
        out = model(x)

end_time = time.perf_counter()

total_time_ms = (end_time - start_time) * 1000
avg_time_ms = total_time_ms / num_runs

print(f"total inference time kr260: {total_time_ms:.6f} ms")
print(f"average inference time kr260: {avg_time_ms:.6f} ms")

if "avg_inference_time_ms_host" in ref:
    print(f"average inference time host: {ref['avg_inference_time_ms_host']:.6f} ms")
    print(f"speed ratio kr260/host: {avg_time_ms / ref['avg_inference_time_ms_host']:.2f} x")