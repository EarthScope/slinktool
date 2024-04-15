ARG BASE=debian:bookworm-slim

# Build slinktool in a separate container, so resulting container does not 
# include compiler tools
FROM $BASE as buildenv
RUN apt update \
    && apt upgrade -y \
    && apt install -y --no-install-recommends \
        build-essential \
    && rm -rf /var/lib/apt/lists/*

# Build executable
COPY . /build
RUN cd /build && make

# Build slinktool container
FROM $BASE
RUN apt update \
    && apt upgrade -y \
    && rm -rf /var/lib/apt/lists/*

# Copy executable and default config from build image
COPY --from=buildenv /build/slinktool /
COPY ./entrypoint.sh /

# Add non-root user
ARG UID=10000
ARG GID=10001
ARG USERNAME=containeruser
RUN \
    groupadd --gid $GID $USERNAME && \
    adduser --uid $UID --gid $GID $USERNAME

# Drop to regular user
USER $USERNAME

# See entrypoint.sh for environment variable support.
ENTRYPOINT [ "/entrypoint.sh" ]