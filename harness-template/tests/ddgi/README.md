# DDGI Contract Checks

`test_project_contract.py` is a dependency-free structural check for a DDGI repository. It verifies the CMake target, expected source roots, and representative DDGI, RT, renderer, SDF, and GLSL files. It does not require a Vulkan-capable GPU.

```powershell
python tests/ddgi/test_project_contract.py
```

Use a different DDGI checkout as the subject when validating a copied harness:

```powershell
python tests/ddgi/test_project_contract.py --project-root D:\path\to\DDGI
```
