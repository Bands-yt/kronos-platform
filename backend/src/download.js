// The Windows installer's real distribution route. KronosSetup.exe (see
// installer/build_installer.iss and
// .github/workflows/build-windows-installer.yml) is a real, single-file
// Inno Setup installer, built and attached to a GitHub Release on every
// v* tag push -- it does not live in this repo or on this server by
// default. This route hands it to a browser two ways, in order, and
// never regresses to a broken download: if neither yields a real file,
// it falls back to the same GitHub Releases page the site linked to
// directly before this route existed.
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { config } from './config.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// An operator can stage a real build here (e.g. copied in at deploy
// time) to skip the GitHub round trip entirely -- same convention as
// storage/local.js's own "a real, working fallback location, no extra
// config required to use it" default.
const localInstallerPath = path.join(__dirname, '..', 'public', 'downloads', 'KronosSetup.exe');

const releasesPageUrl = `https://github.com/${config.githubReleasesRepo}/releases/latest`;
const githubApiLatestReleaseUrl = `https://api.github.com/repos/${config.githubReleasesRepo}/releases/latest`;

// Long enough that a real traffic burst on the download button never
// comes close to GitHub's 60-req/hour unauthenticated-per-IP limit on
// this backend's own outbound calls, short enough that a freshly
// published release's asset shows up within the same session a user
// might refresh and retry in.
const ASSET_CACHE_TTL_MS = 10 * 60 * 1000;

const assetUrlCache = new Map(); // assetKey -> { url, expiresAt }

const ASSET_MATCHERS = {
  windows: (name) => name === 'KronosSetup.exe',
  linux: (name) => /-linux-x64\.tar\.gz$/.test(name),
};

async function resolveGithubAssetUrl(assetKey) {
  const cached = assetUrlCache.get(assetKey);
  if (cached && cached.expiresAt > Date.now()) return cached.url;

  const res = await fetch(githubApiLatestReleaseUrl, {
    headers: { accept: 'application/vnd.github+json', 'user-agent': 'kronos-backend' },
  });
  if (!res.ok) throw new Error(`GitHub releases API returned ${res.status}`);
  const release = await res.json();
  const asset = (release.assets || []).find((a) => ASSET_MATCHERS[assetKey](a.name));
  if (!asset) throw new Error(`the latest release has no ${assetKey} asset (yet)`);

  assetUrlCache.set(assetKey, { url: asset.browser_download_url, expiresAt: Date.now() + ASSET_CACHE_TTL_MS });
  return asset.browser_download_url;
}

export async function downloadWindowsInstaller(_req, res) {
  const hasLocalFile = await fs.stat(localInstallerPath).then((s) => s.isFile()).catch(() => false);
  if (hasLocalFile) {
    return res.download(localInstallerPath, 'KronosSetup.exe');
  }

  try {
    const assetUrl = await resolveGithubAssetUrl('windows');
    return res.redirect(302, assetUrl);
  } catch (err) {
    console.error('[download] could not resolve KronosSetup.exe from GitHub releases: %s', err.message);
  }

  // Never worse than what the site already offered before this route
  // existed -- including the real, current gap where a release's Windows
  // installer job hasn't finished yet.
  res.redirect(302, releasesPageUrl);
}

// Latest Linux build (kronos-<tag>-linux-x64.tar.gz from build.yml), with the
// same releases-page fallback as the Windows route.
export async function downloadLinuxBuild(_req, res) {
  try {
    return res.redirect(302, await resolveGithubAssetUrl('linux'));
  } catch (err) {
    console.error('[download] could not resolve the Linux build from GitHub releases: %s', err.message);
  }
  res.redirect(302, releasesPageUrl);
}
