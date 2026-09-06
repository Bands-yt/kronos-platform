// Dynamic Asset Streaming: the one real chokepoint every package
// upload/download route below goes through. Deliberately NOT reliant
// on S3's own presigned-checksum feature (ChecksumSHA256/Content-MD5
// support and behavior genuinely varies across S3-compatible providers
// -- AWS, MinIO, R2, Backblaze -- and this pipeline needs to work
// against any of them, verified for real against a real MinIO instance
// in this sandbox, not assumed). Instead, verifyObjectHash() below
// re-reads the uploaded object at confirm time and hashes it server-
// side -- a real, simple, universally-portable integrity check that
// costs one extra streamed read per publish, which is not a hot path.
import crypto from 'node:crypto';

import { S3Client, PutObjectCommand, GetObjectCommand, HeadObjectCommand, DeleteObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';

import { config } from '../config.js';
import { packageObjectKey } from './objectKey.js';

export { packageObjectKey };

export const s3Configured = () => Boolean(config.s3Bucket);

let cachedClient = null;
function s3Client() {
  if (!cachedClient) {
    cachedClient = new S3Client({
      region: config.s3Region,
      ...(config.s3Endpoint ? { endpoint: config.s3Endpoint } : {}),
      forcePathStyle: config.s3ForcePathStyle,
      ...(config.s3AccessKeyId
        ? { credentials: { accessKeyId: config.s3AccessKeyId, secretAccessKey: config.s3SecretAccessKey } }
        : {}),
    });
  }
  return cachedClient;
}

export async function createPresignedUploadUrl(key) {
  const command = new PutObjectCommand({ Bucket: config.s3Bucket, Key: key });
  return getSignedUrl(s3Client(), command, { expiresIn: config.packageUploadTtlSeconds });
}

export async function createPresignedDownloadUrl(key) {
  // A public CDN needs no signature at all -- a presigned URL in front
  // of one would just be a link nobody ever needed to expire.
  if (config.s3PublicBaseUrl) return `${config.s3PublicBaseUrl}/${key}`;
  const command = new GetObjectCommand({ Bucket: config.s3Bucket, Key: key });
  return getSignedUrl(s3Client(), command, { expiresIn: config.packageDownloadTtlSeconds });
}

export async function headObject(key) {
  try {
    const result = await s3Client().send(new HeadObjectCommand({ Bucket: config.s3Bucket, Key: key }));
    return { exists: true, sizeBytes: result.ContentLength };
  } catch (err) {
    if (err.name === 'NotFound' || err.$metadata?.httpStatusCode === 404) return { exists: false };
    throw err;
  }
}

// Real server-side verification: streams the object back and hashes it
// -- never trusts the client's own claimed hash, never trusts the
// content-addressed key alone (a key is only as trustworthy as whatever
// put the bytes there). Bounded by config.packageMaxSizeBytes so a
// hostile or corrupt upload can't turn this into an unbounded read.
export async function verifyObjectHash(key, expectedSha256Hex) {
  const response = await s3Client().send(new GetObjectCommand({ Bucket: config.s3Bucket, Key: key }));
  const hash = crypto.createHash('sha256');
  let sizeBytes = 0;
  for await (const chunk of response.Body) {
    sizeBytes += chunk.length;
    if (sizeBytes > config.packageMaxSizeBytes) {
      throw new Error(`Object exceeds the ${config.packageMaxSizeBytes}-byte package size limit.`);
    }
    hash.update(chunk);
  }
  return { matches: hash.digest('hex') === expectedSha256Hex, sizeBytes };
}

export async function deleteObject(key) {
  await s3Client().send(new DeleteObjectCommand({ Bucket: config.s3Bucket, Key: key }));
}

// Telemetry & Minidump Handler: unlike a package/chunk (which a client
// PUTs straight to a presigned URL), a crash dump arrives as this
// service's own request body -- there is no client-facing presign step
// to skip, so this writes the already-received bytes directly instead.
export async function putObjectBuffer(key, buffer) {
  await s3Client().send(new PutObjectCommand({ Bucket: config.s3Bucket, Key: key, Body: buffer }));
}

// Used only by the optional minidump-stackwalk hook (telemetry/
// routes.js) to materialize a stored dump as a real local file for that
// external binary to read -- everything else in this module works with
// keys and streamed bytes alone and never needs a local copy.
export async function getObjectStream(key) {
  const response = await s3Client().send(new GetObjectCommand({ Bucket: config.s3Bucket, Key: key }));
  return response.Body;
}
