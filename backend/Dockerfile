# Kronos API image.
#
# Multi-stage so the shipped image carries production dependencies only --
# no dev tooling, no build cache, nothing extra to audit or exploit.
FROM node:22-alpine AS deps
WORKDIR /app
COPY package.json package-lock.json ./
# npm ci (not install) so the lockfile is authoritative: a production
# image must build the exact tree that was tested, not whatever resolves
# today.
RUN npm ci --omit=dev

FROM node:22-alpine AS runtime
WORKDIR /app

# Run as a non-root user. The node image already ships an unprivileged
# `node` user; a compromised process should not be root inside the
# container.
ENV NODE_ENV=production
COPY --from=deps --chown=node:node /app/node_modules ./node_modules
COPY --chown=node:node package.json ./
COPY --chown=node:node src ./src
COPY --chown=node:node db ./db
COPY --chown=node:node scripts ./scripts
# The storefront dashboard (server.js's own express.static(publicDir)
# call expects this directory to exist at runtime) -- previously
# missing here entirely, which is the real reason a production
# container serving real traffic still 404'd at '/': the file was
# genuinely absent from the image, not merely unrouted.
COPY --chown=node:node public ./public

USER node
EXPOSE 8080

# The container reports its own health, so the reverse proxy and compose
# both learn about a wedged process rather than routing traffic into it.
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD node -e "fetch('http://127.0.0.1:'+(process.env.PORT||8080)+'/healthz').then(r=>process.exit(r.ok?0:1)).catch(()=>process.exit(1))"

CMD ["node", "src/server.js"]
