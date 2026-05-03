import html from 'eslint-plugin-html';

const globals = {
  // browser built-ins
  window:          'readonly',
  document:        'readonly',
  console:         'readonly',
  fetch:           'readonly',
  FormData:        'readonly',
  setTimeout:      'readonly',
  clearInterval:   'readonly',
  setInterval:     'readonly',
  Number:          'readonly',
  Array:           'readonly',
  Set:             'readonly',
  Object:          'readonly',
  Math:            'readonly',
  Infinity:        'readonly',
  Event:           'readonly',
  WebSocket:       'readonly',
  TextEncoder:     'readonly',
  TextDecoder:     'readonly',
  URLSearchParams: 'readonly',
  location:        'readonly',
  history:         'readonly',
};

const commonRules = {
  // strict mode presence
  'strict':              ['error', 'global'],
  // strict mode violations (things that are runtime errors under 'use strict')
  'no-undef':            'error',
  'no-with':             'error',
  'no-dupe-args':        'error',
  'no-delete-var':       'error',
  'no-octal':            'error',
  // other useful checks
  'no-unused-vars':      'warn',
};

export default [
  // JS library files: no implicit globals (everything must be IIFE-wrapped)
  {
    plugins: { html },
    files: ['bt/**/*.js'],
    languageOptions: { ecmaVersion: 2020, sourceType: 'script', globals },
    rules: { ...commonRules, 'no-implicit-globals': 'error' },
  },
  // HTML inline scripts: top-level vars are intentional page-scoped state
  {
    plugins: { html },
    files: ['bt/**/*.html'],
    languageOptions: { ecmaVersion: 2020, sourceType: 'script', globals },
    rules: { ...commonRules, 'no-implicit-globals': 'off' },
  },
];
