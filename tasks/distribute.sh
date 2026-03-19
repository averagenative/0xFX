#!/bin/bash
# distribute.sh — Show task status across all task files
#
# Usage:
#   ./tasks/distribute.sh              # Show summary of all tasks
#   ./tasks/distribute.sh queued       # Show only queued tasks
#   ./tasks/distribute.sh in_progress  # Show only in-progress tasks
#   ./tasks/distribute.sh done         # Show only completed tasks
#   ./tasks/distribute.sh next         # Show next tasks to pick up (lowest phase queued)

set -euo pipefail
TASKS_DIR="$(cd "$(dirname "$0")" && pwd)"
FILTER="${1:-all}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

status_icon() {
    case "$1" in
        queued)      echo -e "${BLUE}○${NC}" ;;
        in_progress) echo -e "${YELLOW}●${NC}" ;;
        blocked)     echo -e "${RED}✗${NC}" ;;
        done)        echo -e "${GREEN}✓${NC}" ;;
        *)           echo "?" ;;
    esac
}

echo ""
echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo -e "${CYAN}  0xFX Task Coordination — Status Report${NC}"
echo -e "${CYAN}═══════════════════════════════════════════${NC}"
echo ""

total=0
queued=0
in_progress=0
blocked=0
done_count=0

for file in "$TASKS_DIR"/{engine,frontend,infra,testing}.md; do
    [ -f "$file" ] || continue
    basename=$(basename "$file" .md)
    echo -e "${YELLOW}── ${basename^^} ──${NC}"

    while IFS= read -r line; do
        if [[ "$line" =~ ^###\ (TASK-[0-9]+|TEST-[0-9]+):\ (.+) ]]; then
            task_id="${BASH_REMATCH[1]}"
            task_name="${BASH_REMATCH[2]}"
            # Read next line for status
            read -r status_line || true
            if [[ "$status_line" =~ Status.*:\ *([a-z_]+) ]]; then
                status="${BASH_REMATCH[1]}"
            else
                status="unknown"
            fi

            total=$((total + 1))
            case "$status" in
                queued)      queued=$((queued + 1)) ;;
                in_progress) in_progress=$((in_progress + 1)) ;;
                blocked)     blocked=$((blocked + 1)) ;;
                done)        done_count=$((done_count + 1)) ;;
            esac

            # Filter
            if [[ "$FILTER" == "all" ]] || [[ "$FILTER" == "$status" ]] || [[ "$FILTER" == "next" && "$status" == "queued" ]]; then
                icon=$(status_icon "$status")
                printf "  %b %-12s %s\n" "$icon" "$task_id" "$task_name"
            fi
        fi
    done < "$file"
    echo ""
done

echo -e "${CYAN}───────────────────────────────────────────${NC}"
printf "  Total: %d  " "$total"
echo -e "${GREEN}✓ Done: $done_count${NC}  ${YELLOW}● Active: $in_progress${NC}  ${BLUE}○ Queued: $queued${NC}  ${RED}✗ Blocked: $blocked${NC}"
if [ "$total" -gt 0 ]; then
    pct=$((done_count * 100 / total))
    echo -e "  Progress: ${pct}%"
fi
echo ""
