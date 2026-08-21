import { config } from '../config.js';

// Integration hook for transactional email.
//
// Deliberately an interface with a logging default rather than a
// hardcoded provider: whichever service is eventually used (SES, Postmark,
// Resend, plain SMTP), it plugs in here without any route needing to
// change. The default transport logs instead of sending, so local
// development never silently mails real people.
let transport = async (message) => {
  console.log('[email:dev] to=%s subject=%s\n%s', message.to, message.subject, message.text);
};

export function setEmailTransport(fn) {
  transport = fn;
}

async function send(message) {
  try {
    await transport(message);
    return true;
  } catch (err) {
    // A failed email must not fail the request that triggered it. Signup
    // still succeeded; the user can request another verification mail.
    console.error('[email] send failed to=%s subject=%s: %s', message.to, message.subject, err.message);
    return false;
  }
}

export function sendVerificationEmail(to, token) {
  const url = `${config.publicBaseUrl}/v1/auth/verify-email?token=${encodeURIComponent(token)}`;
  return send({
    to,
    subject: 'Confirm your Kronos account',
    text: `Welcome to Kronos.\n\nConfirm your email address:\n${url}\n\nThis link expires in 24 hours.`,
  });
}

export function sendPasswordResetEmail(to, token) {
  const url = `${config.publicBaseUrl}/v1/auth/reset-password?token=${encodeURIComponent(token)}`;
  return send({
    to,
    subject: 'Reset your Kronos password',
    text: `Someone asked to reset the password for this Kronos account.\n\n${url}\n\nThis link expires in 1 hour and can only be used once. If this wasn't you, you can ignore this email -- nothing has changed.`,
  });
}
