# Multi-stage build for C++ Backtesting Engine (Docker optimized)
FROM ubuntu:22.04 as builder

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV CMAKE_BUILD_TYPE=Release
ENV CXX_STANDARD=20
ENV DOCKER_BUILD=true

# Install build dependencies - using conservative approach
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    curl \
    git \
    wget \
    libyaml-cpp-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    libcurl4-openssl-dev \
    libeigen3-dev \
    libssl-dev \
    zlib1g-dev \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

# Install additional curl dependencies that are available
RUN apt-get update && apt-get install -y \
    libnghttp2-dev \
    libssh-dev \
    libldap2-dev \
    libkrb5-dev \
    libidn2-dev \
    libpsl-dev \
    libbrotli-dev \
    libzstd-dev \
    && rm -rf /var/lib/apt/lists/* || echo "Some optional curl dependencies not available"

# Install TA-Lib (Technical Analysis Library)
RUN wget http://prdownloads.sourceforge.net/ta-lib/ta-lib-0.4.0-src.tar.gz && \
    tar -xzf ta-lib-0.4.0-src.tar.gz && \
    cd ta-lib/ && \
    ./configure --prefix=/usr && \
    make && \
    make install && \
    cd .. && \
    rm -rf ta-lib ta-lib-0.4.0-src.tar.gz && \
    echo "TA-Lib installation completed. Library files:" && \
    find /usr -name "*ta_lib*" -type f 2>/dev/null && \
    echo "TA-Lib header files:" && \
    find /usr -name "ta_libc.h" -type f 2>/dev/null

# Update library cache
RUN ldconfig

# Verify curl configuration is available for pkg-config
RUN pkg-config --exists libcurl && echo "✓ libcurl pkg-config found" || echo "✗ libcurl pkg-config missing"
RUN pkg-config --cflags --libs libcurl

# Set working directory
WORKDIR /app

# Copy source code
COPY . .

# Make build script executable and build
RUN chmod +x build.sh && \
    ./build.sh

# Runtime stage
FROM ubuntu:22.04

# Set environment variables
ENV DEBIAN_FRONTEND=noninteractive
ENV PORT=8080
ENV RENDER=true

# Install core runtime dependencies
RUN apt-get update && apt-get install -y \
    libyaml-cpp0.7 \
    libcurl4 \
    libssl3 \
    ca-certificates \
    curl \
    wget \
    libboost-system1.74.0 \
    libboost-filesystem1.74.0 \
    libboost-thread1.74.0 \
    zlib1g \
    && rm -rf /var/lib/apt/lists/*

# Install additional curl runtime dependencies that are available
RUN apt-get update && apt-get install -y \
    libnghttp2-14 \
    libssh-4 \
    libldap-2.5-0 \
    libkrb5-3 \
    libidn2-0 \
    libpsl5 \
    libbrotli1 \
    libzstd1 \
    && rm -rf /var/lib/apt/lists/* || echo "Some optional curl runtime dependencies not available"

# Try to install spdlog package, fallback to building from source
RUN apt-get update && apt-get install -y libspdlog1 || \
    (apt-get install -y build-essential cmake git && \
     git clone https://github.com/gabime/spdlog.git && \
     cd spdlog && \
     mkdir build && cd build && \
     cmake .. -DSPDLOG_BUILD_SHARED=ON -DSPDLOG_ENABLE_PCH=OFF && \
     make -j$(nproc) && \
     make install && \
     ldconfig && \
     cd ../.. && \
     rm -rf spdlog && \
     apt-get remove -y build-essential cmake git && \
     apt-get autoremove -y) && \
    rm -rf /var/lib/apt/lists/*

# Copy TA-Lib libraries from builder stage
RUN mkdir -p /usr/lib /usr/local/lib /usr/include /usr/local/include
COPY --from=builder /usr/lib/libta_lib* /usr/lib/
COPY --from=builder /usr/include/ta-lib /usr/include/ta-lib
RUN ldconfig

# Verify that required libraries are available
RUN ldconfig && \
    ldconfig -p | grep spdlog || echo "spdlog library not found in ldconfig" && \
    ldconfig -p | grep yaml-cpp || echo "yaml-cpp library not found in ldconfig" && \
    ldconfig -p | grep ta_lib || echo "ta_lib library not found in ldconfig" && \
    echo "Available spdlog libraries:" && \
    find /usr -name "*spdlog*" 2>/dev/null || echo "No spdlog files found" && \
    echo "Available TA-Lib libraries:" && \
    find /usr -name "*ta_lib*" 2>/dev/null || echo "No TA-Lib files found"

# Create non-root user for security
RUN groupadd -r backtest && useradd -r -g backtest backtest

# Set working directory
WORKDIR /app

# Copy built executable and scripts
COPY --from=builder /app/build/backtest_server ./
COPY debug_libs.sh ./
COPY start.sh ./
RUN chmod +x debug_libs.sh start.sh

# Create data directory for persistent storage
RUN mkdir -p /app/data && \
    chown -R backtest:backtest /app && \
    chmod +x backtest_server

# Test that the executable can find its dependencies
RUN ldd ./backtest_server || echo "ldd failed, but continuing..."

# Switch to non-root user
USER backtest

# Expose port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=60s --retries=3 \
    CMD curl -f -s -m 5 http://localhost:${PORT:-8080}/health | grep -q '"status":"healthy"' || exit 1

# Run the server application with proper signal handling
CMD ["./start.sh"]
