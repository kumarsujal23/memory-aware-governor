FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    gawk \
    bsdmainutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the project files
COPY . .

# Build the project
RUN mkdir -p build && cd build && cmake .. && make -j$(nproc)

# We use the standard entrypoint
CMD ["./eval/run_evaluation.sh", "--duration", "60", "--pressure-mb", "4096"]
