# This file tells pytest to add the repository root to the Python path
# which fixes the "ModuleNotFoundError: No module named 'tests' error.

import sys
import os
sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))
