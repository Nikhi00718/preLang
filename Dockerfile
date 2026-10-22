# A reproducible build of the PrepLang compiler on Ubuntu.
#
#   docker build -t preplang .
#   docker run --rm preplang                     # unit tests, then the students example
#   docker run --rm -it preplang bash            # a shell with preplang on PATH
#   docker run --rm preplang python3 eval/evaluate.py --quick   # the six-question evaluation, small
FROM ubuntu:24.04

RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake ninja-build python3 python3-venv \
 && rm -rf /var/lib/apt/lists/*

# The generated programs need pandas (tables) and OpenCV (images); the release tool needs numpy.
RUN python3 -m venv /opt/venv \
 && /opt/venv/bin/pip install --no-cache-dir numpy pandas pyarrow opencv-python-headless
ENV PATH="/opt/venv/bin:$PATH"

WORKDIR /preplang
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPython3_EXECUTABLE=/opt/venv/bin/python \
 && cmake --build build \
 && ln -s /preplang/build/preplang /usr/local/bin/preplang

CMD ["sh", "-c", "ctest --test-dir build --output-on-failure && preplang examples/tabular/students.prep --run"]
