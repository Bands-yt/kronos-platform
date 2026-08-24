FROM ubuntu:24.04 AS base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    curl \
    ca-certificates \
    libcurl4 \
    libsdl2-2.0-0 \
    libsecret-1-0 && \
    curl -fsSL https://deb.nodesource.com/setup_22.x | bash - && \
    apt-get install -y nodejs

FROM base AS deps
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev

FROM base AS runtime
WORKDIR /app

# Unlike the official node:*-alpine image this Dockerfile used to build
# from, plain ubuntu:24.04 + the Nodesource package ships no unprivileged
# user at all. engine_runtime is spawned as a real child of this same
# process (see sessions/provisioner.js's own spawn() call) -- whatever
# user runs `node src/server.js` is also the user every JIT-spawned
# dedicated server runs as, so real root here means a compromised API
# process (or a malicious game server binary) has root inside the
# container. No explicit --uid/--gid: ubuntu:24.04 is the plain Docker
# Hub image (no pre-existing uid-1000 account the way some cloud/VM
# images ship), but letting useradd pick a free system id sidesteps that
# question entirely.
RUN useradd --system --shell /usr/sbin/nologin --home-dir /app node

COPY --from=deps --chown=node:node /app/node_modules ./node_modules
COPY --chown=node:node package.json ./
COPY --chown=node:node src ./src
COPY --chown=node:node db ./db
COPY --chown=node:node scripts ./scripts
COPY --chown=node:node public ./public
# CrashReporter.cpp (engine/src/core/CrashReporter.cpp) opens its own
# crash_report_*.txt relative to cwd, and a spawned engine_runtime's cwd
# is this same /app (provisioner.js sets no explicit `cwd`) -- `node`
# needs write access to the bare directory itself, not just the files
# --chown above already covers.
RUN chown node:node /app

USER node
EXPOSE 8080

# The container reports its own health, so the reverse proxy and compose
# both learn about a wedged process rather than routing traffic into it.
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||8080)+'/healthz').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["sh", "-c", "node scripts/seed.js && node src/server.js"]
