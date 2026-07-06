FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl wget git ca-certificates \
    python3 python3-pip python3-venv \
    build-essential libssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN curl -LsSf https://astral.sh/uv/install.sh | sh
ENV PATH="/root/.cargo/bin:${PATH}"

RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
ENV PATH="/root/bin:${PATH}"

RUN arduino-cli config init --overwrite && \
    arduino-cli core update-index \
      --additional-urls "https://espressif.github.io/arduino-esp32/package_esp32_index.json" && \
    arduino-cli core install esp32:esp32 \
      --additional-urls "https://espressif.github.io/arduino-esp32/package_esp32_index.json"

RUN arduino-cli lib install "ArduinoJson" && \
    arduino-cli lib install "XPowersLib" && \
    arduino-cli lib install --git-url "https://github.com/moononournation/Arduino_GFX.git"

WORKDIR /workspace
