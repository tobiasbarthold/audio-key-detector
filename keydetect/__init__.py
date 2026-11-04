"""keydetect - fast musical key detection for samples and loops."""

from .detect import KeyResult, analyze, detect_key

__all__ = ["KeyResult", "analyze", "detect_key"]
__version__ = "0.1.0"
