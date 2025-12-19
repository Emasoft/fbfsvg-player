# syntax=docker/dockerfile:1

# =============================================================================
# SVGPlayer Linux Development Docker Image
# =============================================================================
# This Dockerfile creates a complete development environment for building
# Skia and the SVGPlayer SDK on Linux.
#
# Build:
#   docker build -t svgplayer-linux-dev .
#
# Run interactively:
#   docker run -it --rm -v $(pwd)/..:/workspace svgplayer-linux-dev
#
# =============================================================================

FROM ubuntu:24.04

LABEL maintainer="SVGPlayer Development"
LABEL description="Linux development environment for SVGPlayer SDK with Skia"
LABEL version="1.0"

# Prevent interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Set locale to avoid encoding issues
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# =============================================================================
# Install Core Build Tools
# Includes: compilers, build systems, Python, Git, utilities
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    clang \
    lld \
    cmake \
    make \
    ninja-build \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    python3-setuptools \
    git \
    git-lfs \
    curl \
    wget \
    ca-certificates \
    lsb-release \
    gnupg \
    xz-utils \
    file \
    unzip \
    vim \
    gdb \
    valgrind \
    strace \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install Graphics Libraries
# Includes: Mesa OpenGL/EGL, X11, Wayland
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    libegl1-mesa-dev \
    libgles2-mesa-dev \
    libgl1-mesa-dev \
    libglx-dev \
    libglvnd-dev \
    libx11-dev \
    libxcursor-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxi-dev \
    libxext-dev \
    libxfixes-dev \
    libxrender-dev \
    libxcomposite-dev \
    libxdamage-dev \
    libwayland-dev \
    wayland-protocols \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install Font Libraries
# Includes: FreeType, FontConfig, HarfBuzz, test fonts
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    libfreetype6-dev \
    libfreetype-dev \
    libfontconfig1-dev \
    libharfbuzz-dev \
    fonts-dejavu-core \
    fonts-liberation \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install Image Libraries
# Includes: PNG, JPEG, WebP, GIF
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpng-dev \
    libjpeg-turbo8-dev \
    libjpeg-dev \
    libwebp-dev \
    libgif-dev \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install Other Required Libraries
# Includes: compression, XML, ICU, SDL2
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    zlib1g-dev \
    liblz4-dev \
    libzstd-dev \
    libexpat1-dev \
    libicu-dev \
    libsdl2-dev \
    libsdl2-image-dev \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Install Development Extras
# Includes: formatting, linting, documentation, testing tools
# =============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
    clang-format \
    clang-tidy \
    doxygen \
    graphviz \
    linux-tools-common \
    lcov \
    gcovr \
    && rm -rf /var/lib/apt/lists/*

# =============================================================================
# Configure Clang as Default Compiler
# Skia strongly recommends Clang for better performance
# =============================================================================
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100 && \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100

# Set compiler environment variables
ENV CC=clang
ENV CXX=clang++

# =============================================================================
# Create Non-Root User for Development
# Ubuntu 24.04 already has ubuntu:ubuntu with UID/GID 1000
# We reuse it and rename to 'developer' for clarity
# =============================================================================
ARG USERNAME=developer
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN apt-get update && apt-get install -y sudo && \
    if id -u $USER_UID >/dev/null 2>&1; then \
        EXISTING_USER=$(getent passwd $USER_UID | cut -d: -f1); \
        if [ "$EXISTING_USER" != "$USERNAME" ]; then \
            usermod -l $USERNAME $EXISTING_USER; \
            usermod -d /home/$USERNAME -m $USERNAME 2>/dev/null || true; \
            groupmod -n $USERNAME $(getent group $USER_GID | cut -d: -f1) 2>/dev/null || true; \
        fi; \
    else \
        groupadd --gid $USER_GID $USERNAME; \
        useradd --uid $USER_UID --gid $USER_GID -m $USERNAME; \
    fi && \
    echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/$USERNAME && \
    chmod 0440 /etc/sudoers.d/$USERNAME && \
    rm -rf /var/lib/apt/lists/*

# =============================================================================
# Setup Working Directory
# =============================================================================
WORKDIR /workspace

# Create directories for persistent data
RUN mkdir -p /home/$USERNAME/.cache && \
    mkdir -p /home/$USERNAME/.config && \
    chown -R $USERNAME:$USERNAME /home/$USERNAME

# =============================================================================
# Configure Git (safe directory for mounted volumes)
# =============================================================================
RUN git config --system --add safe.directory '*'

# =============================================================================
# Setup depot_tools Path
# depot_tools will be cloned to /workspace/skia-build/depot_tools
# This path is added so 'gclient', 'gn', 'ninja' etc. are available
# =============================================================================
ENV PATH="/workspace/skia-build/depot_tools:${PATH}"

# =============================================================================
# Health Check Script
# =============================================================================
COPY --chmod=755 healthcheck.sh /usr/local/bin/healthcheck.sh

# =============================================================================
# Entry Point
# =============================================================================
COPY --chmod=755 entrypoint.sh /usr/local/bin/entrypoint.sh

USER $USERNAME
WORKDIR /workspace

# Default command - bash shell
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/bash"]
