/*
 * main.c
 * ──────
 * Entry point. Supports two modes:
 *   1. Interactive CLI   —  ./disk_scheduler
 *   2. JSON pipe mode    —  ./disk_scheduler --json ...args...
 *      (called by Python controller; outputs pure JSON to stdout)
 *
 * Subject : CSE-316 Operating Systems | CA2 Project
 */

#include "../include/disk_scheduler.h"

#define JSON_BUFSIZE (1024 * 512)   /* 512 KB — enough for 512 requests × 5 algos */

static void usage(void) {
    fprintf(stderr,
        "Usage:\n"
        "  ./disk_scheduler                          interactive mode\n"
        "  ./disk_scheduler --json [options]         JSON output mode\n\n"
        "Options (JSON mode):\n"
        "  --head     <n>           initial head position\n"
        "  --size     <n>           disk size (cylinders)\n"
        "  --requests <n,n,n,...>   comma-separated request queue\n"
        "  --dir      left|right    SCAN/C-SCAN direction\n"
        "  --algo     all|fcfs|sstf|scan|cscan|look  algorithm to run\n"
    );
}

/* Parse comma-separated int list into array */
static int parse_csv(const char *s, int *out, int maxn) {
    char buf[65536];
    strncpy(buf, s, sizeof(buf)-1);
    int n = 0;
    char *tok = strtok(buf, ",");
    while (tok && n < maxn) { out[n++] = atoi(tok); tok = strtok(NULL, ","); }
    return n;
}

/* ── Interactive mode ───────────────────────────── */
static void interactive(void) {
    SimConfig cfg;
    memset(&cfg, 0, sizeof(SimConfig));

    printf("\n\033[1m\033[96m");
    printf("  ╔══════════════════════════════════════════════╗\n");
    printf("  ║    ADVANCED DISK SCHEDULING SIMULATOR  v%-5s║\n", VERSION);
    printf("  ║    CSE-316 Operating Systems | CA2 Project  ║\n");
    printf("  ╚══════════════════════════════════════════════╝\033[0m\n\n");

    printf("  Disk size (cylinders)  : "); scanf("%d", &cfg.disk_size);
    printf("  Initial head position  : "); scanf("%d", &cfg.head_position);
    printf("  Number of requests     : "); scanf("%d", &cfg.num_requests);
    printf("  Requests (space-sep)   : ");
    int cylinders[MAX_REQUESTS];
    for (int i = 0; i < cfg.num_requests; i++) {
        scanf("%d", &cylinders[i]);
        cfg.requests[i].cylinder   = cylinders[i];
        cfg.requests[i].request_id = i;
    }
    int dirn; printf("  SCAN direction (0=Left, 1=Right): "); scanf("%d", &dirn);
    cfg.direction    = dirn ? DIR_RIGHT : DIR_LEFT;
    cfg.enable_aging = 1;

    printf("\n  Algorithms: 1=FCFS  2=SSTF  3=SCAN  4=C-SCAN  5=LOOK  6=ALL\n");
    printf("  Choice: ");
    int choice; scanf("%d", &choice);

    if (choice == 6) {
        SeekResult results[ALGO_COUNT];
        run_all(&cfg, results);
        for (int i = 0; i < ALGO_COUNT; i++) {
            print_result(&results[i]);
            print_ascii_chart(&results[i]);
        }
        print_comparison(results, ALGO_COUNT);
    } else if (choice >= 1 && choice <= 5) {
        AlgoID id = (AlgoID)(choice - 1);
        SeekResult res;
        switch(id) {
            case ALGO_FCFS:  res = algo_fcfs (&cfg); break;
            case ALGO_SSTF:  res = algo_sstf (&cfg); break;
            case ALGO_SCAN:  res = algo_scan (&cfg); break;
            case ALGO_CSCAN: res = algo_cscan(&cfg); break;
            default:         res = algo_look (&cfg); break;
        }
        print_result(&res);
        print_ascii_chart(&res);
    } else {
        printf("Invalid choice.\n");
    }
}

/* ── JSON mode ──────────────────────────────────── */
static void json_mode(int argc, char *argv[]) {
    SimConfig cfg;
    memset(&cfg, 0, sizeof(SimConfig));
    cfg.disk_size      = 200;
    cfg.head_position  = 53;
    cfg.direction      = DIR_RIGHT;
    cfg.enable_aging   = 1;

    char algo_name[16] = "all";
    int  raw_reqs[MAX_REQUESTS];
    int  n_raw = 0;

    for (int i = 2; i < argc; i++) {
        if      (!strcmp(argv[i],"--head")     && i+1<argc) { cfg.head_position = atoi(argv[++i]); }
        else if (!strcmp(argv[i],"--size")     && i+1<argc) { cfg.disk_size     = atoi(argv[++i]); }
        else if (!strcmp(argv[i],"--dir")      && i+1<argc) { cfg.direction = (!strcmp(argv[++i],"right")) ? DIR_RIGHT : DIR_LEFT; }
        else if (!strcmp(argv[i],"--algo")     && i+1<argc) { strncpy(algo_name, argv[++i], 15); }
        else if (!strcmp(argv[i],"--requests") && i+1<argc) { n_raw = parse_csv(argv[++i], raw_reqs, MAX_REQUESTS); }
    }

    if (n_raw == 0) {
        /* Default textbook example */
        int def[] = {98,183,37,122,14,124,65,67};
        n_raw = 8;
        memcpy(raw_reqs, def, sizeof(def));
    }

    init_config(&cfg, raw_reqs, n_raw,
                cfg.head_position, cfg.disk_size, cfg.direction);

    char *buf = (char *)malloc(JSON_BUFSIZE);
    if (!buf) { fprintf(stderr,"OOM\n"); exit(1); }

    if (!strcmp(algo_name,"all")) {
        SeekResult results[ALGO_COUNT];
        run_all(&cfg, results);
        all_results_to_json(results, ALGO_COUNT, buf, JSON_BUFSIZE);
    } else {
        SeekResult res;
        if      (!strcmp(algo_name,"fcfs"))  res = algo_fcfs (&cfg);
        else if (!strcmp(algo_name,"sstf"))  res = algo_sstf (&cfg);
        else if (!strcmp(algo_name,"scan"))  res = algo_scan (&cfg);
        else if (!strcmp(algo_name,"cscan")) res = algo_cscan(&cfg);
        else                                  res = algo_look (&cfg);
        result_to_json(&res, buf, JSON_BUFSIZE);
    }

    puts(buf);
    free(buf);
}

/* ── Entry ──────────────────────────────────────── */
int main(int argc, char *argv[]) {
    if (argc >= 2 && !strcmp(argv[1], "--json")) {
        json_mode(argc, argv);
    } else if (argc >= 2 && !strcmp(argv[1], "--help")) {
        usage();
    } else {
        interactive();
    }
    return 0;
}
