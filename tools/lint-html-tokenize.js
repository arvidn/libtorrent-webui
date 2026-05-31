#!/usr/bin/env node
// Strict HTML tokenization check via parse5.
//
// Why this exists: html-validate is permissive about tokenizer-level errors.
// It follows the HTML5 spec's "be liberal in what you accept" recovery rules
// and silently fixes up malformed input. That recovery can mask serious bugs
// downstream.
//
// parse5 is the strict HTML5 parser used by jsdom. It exposes an
// `onParseError` callback that fires for every tokenizer/parser error
// (the same errors a browser would log as "Parse error" in the console).
// We fail the commit if any are reported.

"use strict";
const fs = require("fs");
const parse5 = require("parse5");

const files = process.argv.slice(2);
if (files.length === 0) process.exit(0);

let total = 0;
for (const file of files) {
    const src = fs.readFileSync(file, "utf8");
    parse5.parse(src, {
        sourceCodeLocationInfo: true,
        onParseError(err) {
            // err has { code, startLine, startCol, endLine, endCol, ... }.
            console.log(
                `${file}:${err.startLine}:${err.startCol}: error: ${err.code}`,
            );
            total++;
        },
    });
}
if (total > 0) {
    console.log(
        `\n${total} parse error${total === 1 ? "" : "s"} found. ` +
            "These indicate malformed HTML that downstream tools (html-validate, " +
            "prettier) may silently accept while losing visibility into the rest " +
            "of the file. See https://html.spec.whatwg.org/multipage/parsing.html#parse-errors for error codes.",
    );
    process.exit(1);
}
process.exit(0);
