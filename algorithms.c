/*
 * algorithms.c
 * ────────────
 * Implementation of FCFS, SSTF, SCAN, C-SCAN, and LOOK
 * disk scheduling algorithms with:
 *   • Request aging  (starvation prevention)
 *   • Per-step metadata collection
 *   • Full statistical output
 *
 * Subject : CSE-316 Operating Systems | CA2 Project
 */

#include "../include/disk_scheduler.h"

/* ──────────────────────────────────────────────────
 *  UTILITIES
 * ────────────────────────────────────────────────── */

int cmp_int(const void *a, const void *b) {
    int x = *(int *)a, y = *(int *)b;
    return (x > y) - (x < y);
}

/*
 * nearest_unserved()
 * Returns index of the unserved request nearest to `head`.
 * Returns -1 if all requests are served.
 * With aging: if a request has been skipped >= AGING_THRESHOLD
 * times, it gets priority regardless of distance.
 */
int nearest_unserved(const IORequest *reqs, int n, int head) {
    /* First pass: check if any request needs aging priority */
    for (int i = 0; i < n; i++) {
        if (!reqs[i].served && reqs[i].wait_count >= AGING_THRESHOLD)
            return i;
    }
    /* Second pass: normal nearest-first */
    int best_idx  = -1;
    int best_dist = INT_MAX;
    for (int i = 0; i < n; i++) {
        if (!reqs[i].served) {
            int d = abs(reqs[i].cylinder - head);
            if (d < best_dist) { best_dist = d; best_idx = i; }
        }
    }
    return best_idx;
}

/*
 * init_config()
 * Populates a SimConfig from raw cylinder array.
 */
void init_config(SimConfig *cfg, int *cylinders, int n,
                 int head, int disk_size, Direction dir) {
    cfg->num_requests  = n;
    cfg->head_position = head;
    cfg->disk_size     = disk_size;
    cfg->direction     = dir;
    cfg->enable_aging  = 1;
    for (int i = 0; i < n; i++) {
        cfg->requests[i].cylinder    = cylinders[i];
        cfg->requests[i].request_id  = i;
        cfg->requests[i].wait_count  = 0;
        cfg->requests[i].served      = 0;
    }
}

/*
 * compute_statistics()
 * Fills in variance, std_deviation, min/max seek, throughput.
 * Called after each algorithm finishes building its steps[].
 */
void compute_statistics(SeekResult *r) {
    if (r->num_steps == 0) return;

    double sum = 0, sum_sq = 0;
    double mn = (double)INT_MAX, mx = 0;

    for (int i = 0; i < r->num_steps; i++) {
        double d = (double)r->steps[i].distance;
        sum    += d;
        sum_sq += d * d;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
    }

    r->total_seek_distance = (int)sum;
    r->avg_seek_distance   = (r->num_requests > 0) ? sum / r->num_requests : 0.0;
    r->min_seek_distance   = mn;
    r->max_seek_distance   = mx;

    double mean = sum / r->num_steps;
    r->variance     = (sum_sq / r->num_steps) - (mean * mean);
    r->std_deviation = sqrt(r->variance > 0 ? r->variance : 0);
    r->throughput    = (r->total_seek_distance > 0)
                       ? (double)r->num_requests / r->total_seek_distance
                       : 0.0;
}

/* helper: record one seek step */
static void push_step(SeekResult *r, int from, int to, int req_id, int aging) {
    if (r->num_steps >= MAX_SEQ_LEN - 1) return;
    if (r->seq_length >= MAX_SEQ_LEN - 1) return;
    StepInfo *s       = &r->steps[r->num_steps++];
    s->from_cylinder  = from;
    s->to_cylinder    = to;
    s->distance       = abs(to - from);
    s->req_id         = req_id;
    s->starvation_flag= aging;

    r->seek_sequence[r->seq_length++] = to;
}


/* ──────────────────────────────────────────────────
 *  ALGORITHM 1 — FCFS
 *
 *  Concept : Queue-based. Requests served strictly
 *            in the order they were submitted.
 *  OS usage: Simple embedded systems, fairness-critical
 *            scenarios where ordering must be preserved.
 *  Weakness: No optimisation → maximum head movement.
 * ────────────────────────────────────────────────── */
SeekResult algo_fcfs(SimConfig *cfg) {
    SeekResult r;
    memset(&r, 0, sizeof(SeekResult));
    r.algo_id    = ALGO_FCFS;
    r.head_start = cfg->head_position;
    r.disk_size  = cfg->disk_size;
    r.num_requests = cfg->num_requests;
    strcpy(r.algo_name, "FCFS");

    /* Initial position */
    r.seek_sequence[r.seq_length++] = cfg->head_position;

    int cur = cfg->head_position;
    for (int i = 0; i < cfg->num_requests; i++) {
        int next = cfg->requests[i].cylinder;
        push_step(&r, cur, next, cfg->requests[i].request_id, 0);
        cur = next;
    }

    compute_statistics(&r);
    return r;
}


/* ──────────────────────────────────────────────────
 *  ALGORITHM 2 — SSTF  (with aging)
 *
 *  Concept : Greedy — always serve the closest
 *            unserved cylinder next.
 *  OS usage: General-purpose HDD schedulers.
 *  Weakness: Far requests can starve.
 *            Aging prevents infinite starvation.
 * ────────────────────────────────────────────────── */
SeekResult algo_sstf(SimConfig *cfg) {
    SeekResult r;
    memset(&r, 0, sizeof(SeekResult));
    r.algo_id    = ALGO_SSTF;
    r.head_start = cfg->head_position;
    r.disk_size  = cfg->disk_size;
    r.num_requests = cfg->num_requests;
    strcpy(r.algo_name, "SSTF");

    /* Deep-copy requests so we can mark served without touching cfg */
    IORequest reqs[MAX_REQUESTS];
    memcpy(reqs, cfg->requests, sizeof(IORequest) * cfg->num_requests);
    int n = cfg->num_requests;

    r.seek_sequence[r.seq_length++] = cfg->head_position;
    int cur = cfg->head_position;

    for (int served = 0; served < n; served++) {
        /* Increment wait count for all unserved requests */
        for (int i = 0; i < n; i++)
            if (!reqs[i].served) reqs[i].wait_count++;

        int idx    = nearest_unserved(reqs, n, cur);
        if (idx < 0) break;

        int aging  = (reqs[idx].wait_count >= AGING_THRESHOLD) ? 1 : 0;
        if (aging) r.starvation_count++;

        push_step(&r, cur, reqs[idx].cylinder, reqs[idx].request_id, aging);
        cur              = reqs[idx].cylinder;
        reqs[idx].served = 1;
    }

    compute_statistics(&r);
    return r;
}


/* ──────────────────────────────────────────────────
 *  ALGORITHM 3 — SCAN  (Elevator)
 *
 *  Concept : Head moves in one direction, serves all
 *            requests in its path, then reverses.
 *  OS usage: Linux CFQ (Completely Fair Queuing) uses
 *            SCAN-like behaviour as base.
 *  Weakness: Requests near the recently-reversed end
 *            wait nearly a full sweep.
 * ────────────────────────────────────────────────── */
SeekResult algo_scan(SimConfig *cfg) {
    SeekResult r;
    memset(&r, 0, sizeof(SeekResult));
    r.algo_id    = ALGO_SCAN;
    r.head_start = cfg->head_position;
    r.disk_size  = cfg->disk_size;
    r.num_requests = cfg->num_requests;
    strcpy(r.algo_name, "SCAN");

    /* Sort requests */
    int sorted[MAX_REQUESTS];
    for (int i = 0; i < cfg->num_requests; i++)
        sorted[i] = cfg->requests[i].cylinder;
    qsort(sorted, cfg->num_requests, sizeof(int), cmp_int);

    /* Partition around head — <=head goes left, >head goes right */
    int left[MAX_REQUESTS], ln = 0;
    int right[MAX_REQUESTS], rn = 0;
    for (int i = 0; i < cfg->num_requests; i++) {
        if (sorted[i] <= cfg->head_position) left[ln++]  = sorted[i];
        else                                  right[rn++] = sorted[i];
    }

    r.seek_sequence[r.seq_length++] = cfg->head_position;
    int cur = cfg->head_position;

    if (cfg->direction == DIR_RIGHT) {
        for (int i = 0; i < rn; i++) {
            push_step(&r, cur, right[i], -1, 0); cur = right[i];
        }
        /* Sweep to disk end */
        if (cur != cfg->disk_size - 1) {
            push_step(&r, cur, cfg->disk_size - 1, -1, 0);
            cur = cfg->disk_size - 1;
        }
        /* Reverse — serve left side */
        for (int i = ln - 1; i >= 0; i--) {
            push_step(&r, cur, left[i], -1, 0); cur = left[i];
        }
    } else {
        for (int i = ln - 1; i >= 0; i--) {
            push_step(&r, cur, left[i], -1, 0); cur = left[i];
        }
        if (cur != 0) {
            push_step(&r, cur, 0, -1, 0); cur = 0;
        }
        for (int i = 0; i < rn; i++) {
            push_step(&r, cur, right[i], -1, 0); cur = right[i];
        }
    }

    compute_statistics(&r);
    return r;
}


/* ──────────────────────────────────────────────────
 *  ALGORITHM 4 — C-SCAN  (Circular SCAN)
 *
 *  Concept : Head moves in ONE direction only. On
 *            reaching the last cylinder, it jumps
 *            back to cylinder 0 and sweeps again.
 *  OS usage: Provides uniform wait times across all
 *            sectors — preferred in SSDs with custom
 *            FTL (Flash Translation Layer) logic.
 *  Strength: Most uniform latency distribution.
 * ────────────────────────────────────────────────── */
SeekResult algo_cscan(SimConfig *cfg) {
    SeekResult r;
    memset(&r, 0, sizeof(SeekResult));
    r.algo_id    = ALGO_CSCAN;
    r.head_start = cfg->head_position;
    r.disk_size  = cfg->disk_size;
    r.num_requests = cfg->num_requests;
    strcpy(r.algo_name, "C-SCAN");

    int sorted[MAX_REQUESTS];
    for (int i = 0; i < cfg->num_requests; i++)
        sorted[i] = cfg->requests[i].cylinder;
    qsort(sorted, cfg->num_requests, sizeof(int), cmp_int);

    int left[MAX_REQUESTS], ln = 0;
    int right[MAX_REQUESTS], rn = 0;
    for (int i = 0; i < cfg->num_requests; i++) {
        if (sorted[i] <= cfg->head_position) left[ln++]  = sorted[i];
        else                                  right[rn++] = sorted[i];
    }

    r.seek_sequence[r.seq_length++] = cfg->head_position;
    int cur = cfg->head_position;

    if (cfg->direction == DIR_RIGHT) {
        for (int i = 0; i < rn; i++) {
            push_step(&r, cur, right[i], -1, 0); cur = right[i];
        }
        /* Jump to end then wrap to 0 */
        push_step(&r, cur, cfg->disk_size - 1, -1, 0);
        cur = cfg->disk_size - 1;
        push_step(&r, cur, 0, -1, 0);
        cur = 0;
        /* Continue serving left side from 0 upward */
        for (int i = 0; i < ln; i++) {
            push_step(&r, cur, left[i], -1, 0); cur = left[i];
        }
    } else {
        for (int i = ln - 1; i >= 0; i--) {
            push_step(&r, cur, left[i], -1, 0); cur = left[i];
        }
        push_step(&r, cur, 0, -1, 0);           cur = 0;
        push_step(&r, cur, cfg->disk_size-1, -1, 0); cur = cfg->disk_size-1;
        for (int i = rn - 1; i >= 0; i--) {
            push_step(&r, cur, right[i], -1, 0); cur = right[i];
        }
    }

    compute_statistics(&r);
    return r;
}


/* ──────────────────────────────────────────────────
 *  ALGORITHM 5 — LOOK  (BONUS)
 *
 *  Concept : Like SCAN but the head only goes as far
 *            as the last request in each direction —
 *            it does NOT travel to the disk boundary.
 *  OS usage: More efficient than SCAN in practice.
 *            Used in many modern HDD firmware stacks.
 *  Advantage: Shorter travel vs SCAN; fairer vs SSTF.
 * ────────────────────────────────────────────────── */
SeekResult algo_look(SimConfig *cfg) {
    SeekResult r;
    memset(&r, 0, sizeof(SeekResult));
    r.algo_id    = ALGO_LOOK;
    r.head_start = cfg->head_position;
    r.disk_size  = cfg->disk_size;
    r.num_requests = cfg->num_requests;
    strcpy(r.algo_name, "LOOK");

    int sorted[MAX_REQUESTS];
    for (int i = 0; i < cfg->num_requests; i++)
        sorted[i] = cfg->requests[i].cylinder;
    qsort(sorted, cfg->num_requests, sizeof(int), cmp_int);

    int left[MAX_REQUESTS], ln = 0;
    int right[MAX_REQUESTS], rn = 0;
    for (int i = 0; i < cfg->num_requests; i++) {
        if (sorted[i] <= cfg->head_position) left[ln++]  = sorted[i];
        else                                  right[rn++] = sorted[i];
    }

    r.seek_sequence[r.seq_length++] = cfg->head_position;
    int cur = cfg->head_position;

    if (cfg->direction == DIR_RIGHT) {
        /* Serve right — stop at last request, NOT at disk end */
        for (int i = 0; i < rn; i++) {
            push_step(&r, cur, right[i], -1, 0); cur = right[i];
        }
        /* Reverse and serve left */
        for (int i = ln - 1; i >= 0; i--) {
            push_step(&r, cur, left[i], -1, 0); cur = left[i];
        }
    } else {
        for (int i = ln - 1; i >= 0; i--) {
            push_step(&r, cur, left[i], -1, 0); cur = left[i];
        }
        for (int i = 0; i < rn; i++) {
            push_step(&r, cur, right[i], -1, 0); cur = right[i];
        }
    }

    compute_statistics(&r);
    return r;
}


/* ──────────────────────────────────────────────────
 *  BATCH RUNNER
 * ────────────────────────────────────────────────── */
void run_all(SimConfig *cfg, SeekResult out[ALGO_COUNT]) {
    out[ALGO_FCFS]  = algo_fcfs (cfg);
    out[ALGO_SSTF]  = algo_sstf (cfg);
    out[ALGO_SCAN]  = algo_scan (cfg);
    out[ALGO_CSCAN] = algo_cscan(cfg);
    out[ALGO_LOOK]  = algo_look (cfg);
}
