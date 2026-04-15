#!/bin/sh
# Generate /usr/share/nginx/html/config.js from environment variables.
# The file is loaded by index.html before the app bundle so the values
# are available at window.__APP_CONFIG__ without being baked into the image.
cat > /usr/share/nginx/html/config.js <<EOF
window.__APP_CONFIG__ = {
  MSAL_CLIENT_ID: "${MSAL_CLIENT_ID:-}",
  MSAL_TENANT_ID: "${MSAL_TENANT_ID:-}",
  DEBUG: "${DEBUG:-false}",
  AUTOSAVE_INTERVAL: "${AUTOSAVE_INTERVAL:-1500}",
  AUTOSAVE_DEBOUNCE: "${AUTOSAVE_DEBOUNCE:-true}"
};
EOF

exec "$@"
