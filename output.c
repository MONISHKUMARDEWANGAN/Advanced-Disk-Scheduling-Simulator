/*
 * output.c
 * ────────
 * Terminal output, ASCII seek charts, and JSON serialisation.
 * The JSON format is consumed by the Python controller.
 *
 * Subject : CSE-316 Operating Systems | CA2 Project
 */

#include "../include/disk_scheduler.h"

static const char *ALGO_COLOR[] = {
    "\033[96m",   /* FCFS   — cyan    */
    "\033[95m",   /* SSTF   — magenta */
    "\033[92m",   /* SCAN   — green   */
    "\033[93m",   /* C-SCAN — yellow  */
    "\033[94m",   /* LOOK   — blue    */
};
#define RESET "\033[0m"
#define BOLD  "\033[1m"
#define DIM   "\033[2m"

/* ── Terminal result block ───────────────────────── */
void print_result(const SeekResult *r) {
    const char *col = ALGO_COLOR[r->algo_id];
    printf("\n%s%s╔══════════════════════════════════════════════╗%s\n", BOLD, col, RESET);
    printf("%s%s║  %-42s  ║%s\n", BOLD, col, r->algo_name, RESET);
    printf("%s%s╠══════════════════════════════════════════════╣%s\n", BOLD, col, RESET);
    printf("%s║%s  Head Start      : %d\n", col, RESET, r->head_start);
    printf("%s║%s  Disk Size       : %d cylinders\n", col, RESET, r->disk_size);
    printf("%s║%s  Requests        : %d\n", col, RESET, r->num_requests);
    printf("%s╠══════════════════════════════════════════════╣%s\n", col, RESET);
    printf("%s║%s  Seek Sequence:\n", col, RESET);
    printf("  %s[%s%d%s]%s", BOLD, col, r->seek_sequence[0], RESET, RESET);
    for (int i = 1; i < r->seq_length; i++) {
        if (i % 10 == 0) printf("\n  ");
        printf(" %s→%s %d", col, RESET, r->seek_sequence[i]);
    }
    printf("\n");
    printf("%s╠══════════════════════════════════════════════╣%s\n", col, RESET);
    printf("%s║%s  Total Seek Distance : %s%s%d%s\n",       col, RESET, BOLD, col, r->total_seek_distance, RESET);
    printf("%s║%s  Avg Seek / Request  : %.2f\n",   col, RESET, r->avg_seek_distance);
    printf("%s║%s  Std Deviation       : %.2f\n",   col, RESET, r->std_deviation);
    printf("%s║%s  Min Single Seek     : %.0f\n",   col, RESET, r->num_steps > 0 ? r->min_seek_distance : 0.0);
    printf("%s║%s  Max Single Seek     : %.0f\n",   col, RESET, r->max_seek_distance);
    printf("%s║%s  Throughput          : %.6f req/cyl\n", col, RESET, r->throughput);
    if (r->starvation_count > 0)
        printf("%s║%s  Starvation Events   : %d (aging triggered)\n", col, RESET, r->starvation_count);
    printf("%s%s╚══════════════════════════════════════════════╝%s\n", BOLD, col, RESET);
}

/* ── ASCII seek chart ────────────────────────────── */
void print_ascii_chart(const SeekResult *r) {
    int W = 64, dmax = r->disk_size - 1;
    const char *col = ALGO_COLOR[r->algo_id];

    printf("\n  %s[SEEK CHART — %s]%s  head_start=%d  disk=0..%d\n",
           col, r->algo_name, RESET, r->head_start, dmax);
    printf("  %s0%s", DIM, RESET);
    for (int i = 0; i < W - 2; i++) printf("─");
    printf("%s%d%s\n", DIM, dmax, RESET);

    for (int i = 0; i < r->seq_length; i++) {
        int pos = (int)((double)r->seek_sequence[i] / dmax * W);
        printf("  ");
        for (int j = 0; j <= W; j++) {
            if (j == pos) printf("%s█%s", col, RESET);
            else          printf("%s·%s", DIM, RESET);
        }
        printf(" %4d", r->seek_sequence[i]);
        if (i == 0) printf("  (head)");
        printf("\n");

        if (i < r->seq_length - 1) {
            int np = (int)((double)r->seek_sequence[i+1] / dmax * W);
            int lo = (pos < np) ? pos : np;
            int hi = (pos < np) ? np  : pos;
            printf("  ");
            for (int j = 0; j <= W; j++) {
                if (j >= lo && j <= hi) printf("%s─%s", col, RESET);
                else                     printf(" ");
            }
            printf("\n");
        }
    }
}

/* ── Comparison table ────────────────────────────── */
void print_comparison(const SeekResult results[], int count) {
    printf("\n%s╔══════════════════════════════════════════════════════════════════╗%s\n", BOLD, RESET);
    printf("%s║                  ALGORITHM COMPARISON SUMMARY                   ║%s\n", BOLD, RESET);
    printf("%s╠════════╦═══════════╦══════════════╦═══════════╦═══════════╦═════╣%s\n", BOLD, RESET);
    printf("%s║ Algo   ║ TotalSeek ║ Avg/Req      ║ Std Dev   ║ Throughput║Best ║%s\n", BOLD, RESET);
    printf("%s╠════════╬═══════════╬══════════════╬═══════════╬═══════════╬═════╣%s\n", BOLD, RESET);

    /* Find best */
    int best_dist = INT_MAX, best_idx = 0;
    for (int i = 0; i < count; i++)
        if (results[i].total_seek_distance < best_dist) {
            best_dist = results[i].total_seek_distance; best_idx = i;
        }

    for (int i = 0; i < count; i++) {
        const char *col = ALGO_COLOR[results[i].algo_id];
        char star = (i == best_idx) ? '*' : ' ';
        printf("║ %s%-6s%s ║ %s%-9d%s ║ %s%-12.2f%s ║ %s%-9.2f%s ║ %-9.4f  ║ %s%c%s   ║\n",
               col, results[i].algo_name, RESET,
               col, results[i].total_seek_distance, RESET,
               col, results[i].avg_seek_distance, RESET,
               col, results[i].std_deviation, RESET,
               results[i].throughput,
               (i==best_idx)?"\033[92m":DIM, star, RESET);
    }
    printf("%s╚════════╩═══════════╩══════════════╩═══════════╩═══════════╩═════╝%s\n", BOLD, RESET);
    printf("  * Best performer (minimum total seek distance)\n\n");
}

/* ── JSON serialiser ─────────────────────────────── */
int result_to_json(const SeekResult *r, char *buf, int bufsz) {
    int w = 0;

    w += snprintf(buf+w, bufsz-w,
        "{"
        "\"algorithm\":\"%s\","
        "\"head_start\":%d,"
        "\"disk_size\":%d,"
        "\"num_requests\":%d,"
        "\"total_seek_distance\":%d,"
        "\"avg_seek_distance\":%.4f,"
        "\"min_seek_distance\":%.1f,"
        "\"max_seek_distance\":%.1f,"
        "\"variance\":%.4f,"
        "\"std_deviation\":%.4f,"
        "\"throughput\":%.8f,"
        "\"starvation_count\":%d,"
        "\"seq_length\":%d,"
        "\"seek_sequence\":[",
        r->algo_name,
        r->head_start,
        r->disk_size,
        r->num_requests,
        r->total_seek_distance,
        r->avg_seek_distance,
        (r->num_steps > 0) ? r->min_seek_distance : 0.0,
        r->max_seek_distance,
        r->variance,
        r->std_deviation,
        r->throughput,
        r->starvation_count,
        r->seq_length
    );

    for (int i = 0; i < r->seq_length; i++)
        w += snprintf(buf+w, bufsz-w, "%s%d", i?",":"", r->seek_sequence[i]);

    w += snprintf(buf+w, bufsz-w, "],\"steps\":[");
    for (int i = 0; i < r->num_steps; i++) {
        const StepInfo *s = &r->steps[i];
        w += snprintf(buf+w, bufsz-w,
            "%s{\"from\":%d,\"to\":%d,\"dist\":%d,\"req_id\":%d,\"aging\":%d}",
            i?",":"",
            s->from_cylinder, s->to_cylinder,
            s->distance, s->req_id, s->starvation_flag);
    }
    w += snprintf(buf+w, bufsz-w, "]}");
    return w;
}

int all_results_to_json(const SeekResult results[], int count, char *buf, int bufsz) {
    int w = snprintf(buf, bufsz, "[");
    for (int i = 0; i < count; i++) {
        if (i) w += snprintf(buf+w, bufsz-w, ",");
        w += result_to_json(&results[i], buf+w, bufsz-w);
    }
    w += snprintf(buf+w, bufsz-w, "]");
    return w;
}
