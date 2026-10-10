# TensorFlow Data Validation for eval/baselines.py (TFDV has no Windows build).
#   docker build -t preplang-tfdv -f eval/tfdv.Dockerfile eval
#   docker run --rm -v "%CD%:/work" -w /work preplang-tfdv python eval/baselines.py OUT ANCHOR BATCH... --tools tfdv
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends python3-venv && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/venv
ENV PATH=/opt/venv/bin:$PATH
RUN pip install --no-cache-dir tensorflow-data-validation==1.21.0 pyarrow
