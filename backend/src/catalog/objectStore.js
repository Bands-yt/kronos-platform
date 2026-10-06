// One interface over S3 and the local-disk fallback, so catalog routes do not
// branch on the storage backend at every call.
import { config } from '../config.js';
import * as s3 from '../storage/s3.js';
import * as local from '../storage/local.js';

export const usingS3 = () => s3.s3Configured();

export async function head(key) {
  return usingS3() ? s3.headObject(key) : local.headObject(key);
}

export async function openStream(key) {
  return usingS3() ? s3.getObjectStream(key) : local.readObjectStream(key);
}

export async function remove(key) {
  return usingS3() ? s3.deleteObject(key) : local.deleteObject(key);
}

export async function readAll(key, maxBytes) {
  const chunks = [];
  let size = 0;
  for await (const chunk of await openStream(key)) {
    size += chunk.length;
    if (size > maxBytes) return null;
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }
  return Buffer.concat(chunks);
}

// `localPath` is this service's own PUT route used when no bucket is configured.
export async function uploadTarget(key, localPath) {
  if (usingS3()) {
    return { upload_url: await s3.createPresignedUploadUrl(key), expires_in: config.packageUploadTtlSeconds, storage: 's3' };
  }
  return { upload_url: `${config.publicBaseUrl}${localPath}`, expires_in: null, storage: 'local' };
}

export async function downloadUrl(key, localPath, { contentType } = {}) {
  if (usingS3()) {
    return {
      url: await s3.createPresignedDownloadUrl(key, { contentType }),
      expires_in: config.s3PublicBaseUrl ? null : config.packageDownloadTtlSeconds,
    };
  }
  return { url: `${config.publicBaseUrl}${localPath}`, expires_in: null };
}

export async function storeLocalUpload(key, readable, maxBytes) {
  return local.writeObjectFromStream(key, readable, maxBytes);
}

export function streamLocal(key) {
  return local.readObjectStream(key);
}
