#!/usr/bin/env python3
"""Export a Crank P1 PyTorch checkpoint to an ONNX deployment bundle.

The standalone CLI reconstructs the CNN v1/v2/v3 architecture used by
Crank_P1_CNN(20260818-055105).py and the standard torchvision ResNet18 used in
the Crank P1 experiments.  For any other architecture (including a custom NN),
call export_loaded_model() from the training script while the exact model
instance is still available.
"""

from __future__ import annotations

import argparse
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np
import torch
import torch.nn as nn


DEFAULT_COMMANDS = [3, 4, 5, 6, 7, 100, 900]
IMAGENET_MEAN = [0.485, 0.456, 0.406]
IMAGENET_STD = [0.229, 0.224, 0.225]


class CNN(nn.Module):
    """Exact CNN definition from Crank_P1_CNN(20260818-055105).py."""

    def __init__(
        self,
        num_classes: int,
        model_version: str,
        input_channels: int,
    ) -> None:
        super().__init__()

        if model_version == "v1":
            self.features = nn.Sequential(
                nn.Conv2d(input_channels, 16, 3, padding=1),
                nn.BatchNorm2d(16),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(16, 32, 3, padding=1),
                nn.BatchNorm2d(32),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(32, 64, 3, padding=1),
                nn.BatchNorm2d(64),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(64, 128, 3, padding=1),
                nn.BatchNorm2d(128),
                nn.ReLU(),
                nn.AdaptiveAvgPool2d((1, 1)),
            )
            classifier_input = 128
        elif model_version in ("v2", "v3"):
            pool_size = (1, 1) if model_version == "v2" else (4, 3)
            classifier_input = 256 if model_version == "v2" else 256 * 4 * 3
            self.features = nn.Sequential(
                nn.Conv2d(input_channels, 16, 3, padding=1),
                nn.BatchNorm2d(16),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(16, 32, 3, padding=1),
                nn.BatchNorm2d(32),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(32, 64, 3, padding=1),
                nn.BatchNorm2d(64),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(64, 128, 3, padding=1),
                nn.BatchNorm2d(128),
                nn.ReLU(),
                nn.MaxPool2d(2),
                nn.Conv2d(128, 256, 3, padding=1),
                nn.BatchNorm2d(256),
                nn.ReLU(),
                nn.AdaptiveAvgPool2d(pool_size),
            )
        else:
            raise ValueError(f"unknown CNN model version: {model_version}")

        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(classifier_input, 64),
            nn.ReLU(),
            nn.Dropout(0.3),
            nn.Linear(64, num_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.classifier(self.features(x))


def _state_dict_from_checkpoint(checkpoint: Any) -> dict[str, torch.Tensor]:
    if isinstance(checkpoint, Mapping):
        if "model" in checkpoint and isinstance(checkpoint["model"], Mapping):
            state = checkpoint["model"]
        elif "state_dict" in checkpoint and isinstance(checkpoint["state_dict"], Mapping):
            state = checkpoint["state_dict"]
        elif checkpoint and all(torch.is_tensor(v) for v in checkpoint.values()):
            state = checkpoint
        else:
            raise ValueError("checkpoint does not contain 'model' or 'state_dict'")
    else:
        raise ValueError("checkpoint must be a dictionary")

    cleaned: dict[str, torch.Tensor] = {}
    for key, value in state.items():
        new_key = str(key)
        for prefix in ("module.", "_orig_mod."):
            if new_key.startswith(prefix):
                new_key = new_key[len(prefix) :]
        cleaned[new_key] = value
    return cleaned


def _metadata(checkpoint: Any) -> Mapping[str, Any]:
    return checkpoint if isinstance(checkpoint, Mapping) else {}


def _commands_from_metadata(
    metadata: Mapping[str, Any],
    fallback: Sequence[int] | None,
) -> list[int]:
    cmd_to_label = metadata.get("cmd_to_label")
    label_to_cmd = metadata.get("label_to_cmd")

    if isinstance(cmd_to_label, Mapping) and cmd_to_label:
        pairs = sorted((int(label), int(cmd)) for cmd, label in cmd_to_label.items())
    elif isinstance(label_to_cmd, Mapping) and label_to_cmd:
        pairs = sorted((int(label), int(cmd)) for label, cmd in label_to_cmd.items())
    elif fallback:
        pairs = list(enumerate(int(v) for v in fallback))
    else:
        raise ValueError("command mapping is missing; pass --commands")

    expected = list(range(len(pairs)))
    actual = [label for label, _ in pairs]
    if actual != expected:
        raise ValueError(f"labels must be consecutive from 0; got {actual}")
    return [cmd for _, cmd in pairs]


def _detect_architecture(state: Mapping[str, torch.Tensor], override: str) -> str:
    if override != "auto":
        return override
    if "fc.weight" in state and any(key.startswith("layer1.") for key in state):
        return "resnet18"
    if "features.0.weight" in state and "classifier.4.weight" in state:
        return "cnn"
    raise ValueError(
        "could not identify the model architecture. Use --architecture, or call "
        "export_loaded_model() from the training script for a custom NN."
    )


def _infer_cnn_version(state: Mapping[str, torch.Tensor], metadata: Mapping[str, Any]) -> str:
    model_type = str(metadata.get("model_type", "")).lower()
    for version in ("v1", "v2", "v3"):
        if version in model_type:
            return version

    classifier_width = int(state["classifier.1.weight"].shape[1])
    if classifier_width == 128:
        return "v1"
    if classifier_width == 256:
        return "v2"
    if classifier_width == 256 * 4 * 3:
        return "v3"
    raise ValueError(f"cannot infer CNN version from classifier width {classifier_width}")


def _build_model(
    architecture: str,
    state: Mapping[str, torch.Tensor],
    metadata: Mapping[str, Any],
) -> tuple[nn.Module, int, str]:
    if architecture == "cnn":
        version = _infer_cnn_version(state, metadata)
        input_channels = int(state["features.0.weight"].shape[1])
        num_classes = int(state["classifier.4.weight"].shape[0])
        model = CNN(num_classes, version, input_channels)
        model_type = str(metadata.get("model_type", f"CNN_{version}"))
        return model, input_channels, model_type

    if architecture == "resnet18":
        from torchvision.models import resnet18

        input_channels = int(state["conv1.weight"].shape[1])
        num_classes = int(state["fc.weight"].shape[0])
        model = resnet18(weights=None)
        if input_channels != 3:
            model.conv1 = nn.Conv2d(
                input_channels,
                64,
                kernel_size=7,
                stride=2,
                padding=3,
                bias=False,
            )
        model.fc = nn.Linear(model.fc.in_features, num_classes)
        model_type = str(metadata.get("model_type", "ResNet18"))
        return model, input_channels, model_type

    raise ValueError(
        "Standalone reconstruction supports cnn and resnet18. For a custom NN, "
        "call export_loaded_model() from its training script."
    )


def _yaml_string(value: str) -> str:
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _write_config(
    path: Path,
    *,
    model_type: str,
    image_height: int,
    image_width: int,
    image_mode: str,
    input_channels: int,
    mean: Sequence[float],
    std: Sequence[float],
    commands: Sequence[int],
    min_confidence: float,
) -> None:
    def sequence(values: Sequence[float | int]) -> str:
        return "[ " + ", ".join(str(v) for v in values) + " ]"

    text = "\n".join(
        [
            "%YAML:1.0",
            "---",
            f"model_type: {_yaml_string(model_type)}",
            f"image_height: {image_height}",
            f"image_width: {image_width}",
            f"image_mode: {_yaml_string(image_mode)}",
            f"input_channels: {input_channels}",
            f"mean: {sequence(mean)}",
            f"std: {sequence(std)}",
            f"commands: {sequence(commands)}",
            f"min_confidence: {min_confidence:.6f}",
            "",
        ]
    )
    path.write_text(text, encoding="utf-8")


def _export_and_verify(model: nn.Module, dummy: torch.Tensor, onnx_path: Path) -> None:
    model = model.cpu().eval()
    with torch.no_grad():
        torch_output = model(dummy).cpu().numpy()

    # Opset 13 and the legacy graph exporter give broad compatibility with the
    # OpenCV 4.x versions commonly installed on Ubuntu 22.04/KR260 images.
    torch.onnx.export(
        model,
        dummy,
        str(onnx_path),
        input_names=["image"],
        output_names=["logits"],
        opset_version=13,
        do_constant_folding=True,
        dynamo=False,
    )

    try:
        import onnx

        onnx.checker.check_model(onnx.load(str(onnx_path)))
        print("ONNX checker: OK")
    except ImportError:
        print("ONNX checker: skipped (onnx is not installed)")

    try:
        import cv2

        net = cv2.dnn.readNetFromONNX(str(onnx_path))
        net.setInput(dummy.numpy())
        opencv_output = net.forward()
        max_abs_error = float(np.max(np.abs(torch_output - opencv_output)))
        same_class = int(torch_output.argmax()) == int(opencv_output.argmax())
        print(f"OpenCV DNN max abs error: {max_abs_error:.8g}")
        print(f"OpenCV DNN argmax match: {same_class}")
        if not same_class or max_abs_error > 1.0e-3:
            raise RuntimeError("OpenCV DNN output does not match PyTorch output")
    except ImportError:
        print("OpenCV DNN check: skipped (opencv-python is not installed)")


def export_loaded_model(
    *,
    model: nn.Module,
    checkpoint: Mapping[str, Any],
    onnx_path: str | Path,
    yaml_path: str | Path,
    commands: Sequence[int] | None = None,
    image_height: int | None = None,
    image_width: int | None = None,
    image_mode: str | None = None,
    input_channels: int | None = None,
    model_type: str | None = None,
    min_confidence: float = 0.50,
    normalization: str | None = None,
) -> None:
    """Export an already-created model, including a custom NN architecture."""

    metadata = _metadata(checkpoint)
    resolved_commands = _commands_from_metadata(metadata, commands)
    height = int(image_height or metadata.get("img_height", 0))
    width = int(image_width or metadata.get("img_width", 0))
    channels = int(input_channels or metadata.get("input_channels", 0))
    mode = str(image_mode or metadata.get("image_mode", "")).upper()
    resolved_model_type = str(model_type or metadata.get("model_type", model.__class__.__name__))

    if height <= 0 or width <= 0 or channels not in (1, 3):
        raise ValueError("valid image_height, image_width, and input_channels are required")
    if mode not in ("GRAY", "RGB"):
        raise ValueError("image_mode must be GRAY or RGB")

    if normalization is None:
        normalization = "imagenet" if "resnet" in resolved_model_type.lower() else "none"
    if normalization == "imagenet":
        if channels != 3:
            raise ValueError("ImageNet normalization requires three input channels")
        mean, std = IMAGENET_MEAN, IMAGENET_STD
    elif normalization == "none":
        mean, std = [0.0] * channels, [1.0] * channels
    else:
        raise ValueError("normalization must be 'none' or 'imagenet'")

    onnx_path = Path(onnx_path)
    yaml_path = Path(yaml_path)
    onnx_path.parent.mkdir(parents=True, exist_ok=True)
    yaml_path.parent.mkdir(parents=True, exist_ok=True)

    dummy = torch.zeros(1, channels, height, width, dtype=torch.float32)
    _export_and_verify(model, dummy, onnx_path)

    with torch.no_grad():
        output = model.cpu().eval()(dummy)
    if output.ndim != 2 or output.shape[0] != 1:
        raise ValueError(f"model output must be [1, classes], got {tuple(output.shape)}")
    if int(output.shape[1]) != len(resolved_commands):
        raise ValueError(
            f"model classes ({output.shape[1]}) do not match commands ({len(resolved_commands)})"
        )

    _write_config(
        yaml_path,
        model_type=resolved_model_type,
        image_height=height,
        image_width=width,
        image_mode=mode,
        input_channels=channels,
        mean=mean,
        std=std,
        commands=resolved_commands,
        min_confidence=min_confidence,
    )
    print(f"ONNX: {onnx_path}")
    print(f"YAML: {yaml_path}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pth", required=True, type=Path)
    parser.add_argument("--onnx", type=Path)
    parser.add_argument("--yaml", type=Path)
    parser.add_argument(
        "--architecture",
        choices=["auto", "cnn", "resnet18"],
        default="auto",
    )
    parser.add_argument("--height", type=int)
    parser.add_argument("--width", type=int)
    parser.add_argument("--image-mode", choices=["GRAY", "RGB"])
    parser.add_argument("--commands", nargs="+", type=int)
    parser.add_argument("--min-confidence", type=float, default=0.50)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    checkpoint = torch.load(args.pth, map_location="cpu", weights_only=False)
    metadata = _metadata(checkpoint)
    state = _state_dict_from_checkpoint(checkpoint)
    architecture = _detect_architecture(state, args.architecture)
    model, input_channels, model_type = _build_model(architecture, state, metadata)
    model.load_state_dict(state, strict=True)

    height = int(args.height or metadata.get("img_height", 320))
    width = int(args.width or metadata.get("img_width", 180))
    image_mode = str(
        args.image_mode
        or metadata.get("image_mode", "GRAY" if input_channels == 1 else "RGB")
    ).upper()

    onnx_path = args.onnx or args.pth.with_suffix(".onnx")
    yaml_path = args.yaml or args.pth.with_suffix(".yaml")

    export_loaded_model(
        model=model,
        checkpoint=metadata,
        onnx_path=onnx_path,
        yaml_path=yaml_path,
        commands=args.commands,
        image_height=height,
        image_width=width,
        image_mode=image_mode,
        input_channels=input_channels,
        model_type=model_type,
        min_confidence=args.min_confidence,
        normalization="imagenet" if architecture == "resnet18" else "none",
    )


if __name__ == "__main__":
    main()
