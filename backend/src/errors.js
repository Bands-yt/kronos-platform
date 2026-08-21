export class HttpError extends Error {
  constructor(status, code, message, details) {
    super(message);
    this.status = status;
    this.code = code;
    this.details = details;
  }
}

export const badRequest = (msg, details) => new HttpError(400, 'bad_request', msg, details);
export const unauthorized = (msg = 'Authentication required.') => new HttpError(401, 'unauthorized', msg);
export const forbidden = (msg = 'Not permitted.') => new HttpError(403, 'forbidden', msg);
export const notFound = (msg = 'Not found.') => new HttpError(404, 'not_found', msg);
export const conflict = (msg) => new HttpError(409, 'conflict', msg);
export const tooManyRequests = (msg = 'Too many requests.') => new HttpError(429, 'rate_limited', msg);
export const serviceUnavailable = (msg) => new HttpError(503, 'unavailable', msg);

// Wraps an async route so a rejected promise becomes a real next(err)
// instead of an unhandled rejection that silently hangs the request.
export const asyncRoute = (fn) => (req, res, next) => Promise.resolve(fn(req, res, next)).catch(next);
