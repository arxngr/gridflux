#!/bin/sh
# Setup git hooks for the repository

git config core.hooksPath scripts/git-hooks
echo "Git hooks configured to use scripts/git-hooks"
chmod +x scripts/git-hooks/* 2>/dev/null
echo "Hooks made executable"
