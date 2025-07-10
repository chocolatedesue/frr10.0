# syntax=docker/dockerfile:1

# =============================================================================
# Base Alpine Builder Stage
# Sets up the basic environment for building APK packages
# =============================================================================
FROM alpine:3.20 AS alpine-builder

# Install build dependencies and configure system
RUN apk add \
    --update-cache \
    abuild \
    alpine-conf \
    alpine-sdk \
    doas \
    && setup-apkcache /var/cache/apk \
    && mkdir -p /pkgs/apk \
    && echo 'builder ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers \
    && echo 'permit nopass builder' >> /etc/doas.d/doas.conf

# Create builder user and generate signing keys
RUN adduser -D -G abuild builder

# Generate and install signing keys automatically
RUN su builder -c 'abuild-keygen -a -i -n'

# =============================================================================
# libyang APK Builder Stage
# Builds the libyang library as a dependency for FRR
# =============================================================================
FROM alpine-builder AS libyang-builder

# Set up libyang source
RUN mkdir -p /src/libyang
COPY docker/alpine/libyang/APKBUILD /src/libyang
RUN chown -R builder /pkgs /src

# Build libyang APK
USER builder
RUN cd /src/libyang \
    && abuild checksum \
    && git init \
    && abuild -r -P /pkgs/apk -K

# =============================================================================
# FRR Source Distribution Builder Stage
# Creates the FRR source distribution tarball
# =============================================================================
FROM alpine:3.20 AS source-builder

# Prepare directories
RUN mkdir -p /src/alpine /pkgs/apk

# Copy APKBUILD template and libyang packages
COPY alpine/APKBUILD.in /src/alpine
COPY --from=libyang-builder /pkgs/apk/src /pkgs/apk

# Install libyang packages
RUN cd /pkgs/apk && apk add --allow-untrusted */*.apk

# Install build dependencies
RUN source /src/alpine/APKBUILD.in \
    && apk add \
        --no-cache \
        --update-cache \
        $makedepends
	

# RUN apk add  --no-cache  --update-cache sqlite

# Copy source code and build distribution
COPY . /src
ARG PKGVER

RUN cd /src \
    && ./bootstrap.sh \
    &&   ./configure \
                --prefix=/usr \
                --sysconfdir=/etc \
                --localstatedir=/var \
                --sbindir=/usr/lib/frr \
                --enable-sharpd \
                --enable-multipath=64 \
                --enable-user=frr \
                --enable-group=frr \
                --enable-vty-group=frrvty \
                --enable-snmp=agentx \
                --enable-scripting \
                --enable-numeric-version \
				--with-pkg-extra-version="_git$PKGVER" \
    && make -j$(nproc) dist

# =============================================================================
# FRR APK Builder Stage
# Builds the FRR APK from the source distribution
# =============================================================================
FROM alpine-builder AS frr-builder

# Copy source distribution and dependencies
COPY --from=source-builder /src/frr-*.tar.gz /src/alpine/* /dist/
COPY --from=libyang-builder /pkgs/apk/src /pkgs/apk

# Install libyang packages and clean up indices
RUN cd /pkgs/apk && apk add --allow-untrusted */*.apk
RUN find /pkgs/apk -type f -name APKINDEX.tar.gz -delete

# Set up permissions
RUN chown -R builder /dist /pkgs

# Build FRR APK
USER builder
RUN cd /dist \
    && abuild checksum \
    && git init \
    && abuild -r -P /pkgs/apk -K

# =============================================================================
# Final Runtime Stage
# Creates the final FRR runtime image
# =============================================================================
FROM alpine:3.20

# Install runtime dependencies
RUN apk add \
    --no-cache \
    --update-cache \
    tini

# Copy and install FRR packages
RUN mkdir -p /pkgs/apk
COPY --from=frr-builder /pkgs/apk/ /pkgs/apk/
RUN apk add \
    --no-cache \
    --allow-untrusted \
    /pkgs/apk/*/*.apk \
    && rm -rf /pkgs

# Set up FRR runtime environment
RUN mkdir -p /var/run/frr \
    && chown -R frr:frr /etc/frr /var/run/frr \
    && apk add iperf3 --no-cache --update-cache 


# Copy startup script
COPY docker/alpine/docker-start /usr/lib/frr/docker-start

# Configure container runtime
ENTRYPOINT ["/sbin/tini", "--"]
CMD ["/usr/lib/frr/docker-start"]