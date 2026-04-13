# ══════════════════════════════════════════════════════
#  Makefile — Advanced Disk Scheduling Simulator v2.0
#  CSE-316 Operating Systems | CA2 Project
# ══════════════════════════════════════════════════════

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -I core/include
LDFLAGS = -lm
SRCS    = core/src/main.c core/src/algorithms.c core/src/output.c
TARGET  = core/disk_scheduler

.PHONY: all clean test dashboard run

all: $(TARGET)
	@echo "✓  C engine compiled  →  $(TARGET)"

$(TARGET): $(SRCS) core/include/disk_scheduler.h
	$(CC) $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

# Run interactive terminal mode
run: all
	./$(TARGET)

# Textbook test — verify against known GATE answers
test: all
	@echo "\n── Textbook Example: head=53, disk=200, requests=[98,183,37,122,14,124,65,67] ──"
	@./$(TARGET) --json --head 53 --size 200 \
		--requests "98,183,37,122,14,124,65,67" --dir right \
		| python3 -c "
import json,sys
d=json.load(sys.stdin)
print(f'  {\"Algo\":<8} {\"TotalSeek\":>10} {\"Avg\":>8} {\"StdDev\":>8} {\"Best\":<5}')
print(f'  {\"-\"*44}')
for r in d:
    b=' ★' if r[\"total_seek_distance\"]==min(x[\"total_seek_distance\"] for x in d) else ''
    print(f'  {r[\"algorithm\"]:<8} {r[\"total_seek_distance\"]:>10} {r[\"avg_seek_distance\"]:>8.2f} {r[\"std_deviation\"]:>8.2f}{b}')
"

# Launch full Python + web dashboard
dashboard: all
	@echo "Launching dashboard on http://localhost:8080 ..."
	cd controller && python3 simulator.py dashboard

# Python CLI only (no browser)
py-run: all
	cd controller && python3 simulator.py run \
		--head 53 --size 200 \
		--requests "98,183,37,122,14,124,65,67" \
		--direction right

clean:
	rm -f $(TARGET)
	rm -f controller/output/*.csv
	@echo "✓  Clean"
