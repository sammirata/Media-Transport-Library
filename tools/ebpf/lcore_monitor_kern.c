/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
 * Copyright(c) 2023 Intel Corporation
 */

//clang-format off
#include "vmlinux.h"
//clang-format off

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "lcore_monitor.h"

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 1);
  __type(key, u32);
  __type(value, struct lcore_tid_cfg);
} lm_cfg_map SEC(".maps");

struct {
  __uint(type, BPF_MAP_TYPE_RINGBUF);
  __uint(max_entries, 512 * 1024);
} lm_events_map SEC(".maps");

static struct lcore_tid_cfg* lm_get_cfg(void) {
  struct lcore_tid_cfg* cfg;
  uint32_t key = 0;
  cfg = bpf_map_lookup_elem(&lm_cfg_map, &key);
  return cfg;
}

static int lm_switch_event_submit(enum lcore_tid_event_type type,
                                  struct trace_event_raw_sched_switch* args) {
  struct lcore_tid_event* e;

  e = bpf_ringbuf_reserve(&lm_events_map, sizeof(*e), 0);
  if (!e) {
    char fmt[] = "lm event ringbuf reserve fail\n";
    bpf_trace_printk(fmt, sizeof(fmt));
    return 0;
  }

  e->type = type;
  e->ns = bpf_ktime_get_ns();
  e->next_pid = args->next_pid;

  bpf_ringbuf_submit(e, 0);
  return 0;
}

SEC("tracepoint/sched/sched_switch")
int bpf_prog_sched_switch(struct trace_event_raw_sched_switch* args) {
  struct lcore_tid_cfg* cfg = lm_get_cfg();
  if (!cfg) return 0;

  /* core id check */
  if (bpf_get_smp_processor_id() != cfg->core_id) return 0;

  if (cfg->bpf_trace) {
    char fmt[] = "sched_switch: prev_pid %d next_pid in\n";
    bpf_trace_printk(fmt, sizeof(fmt), args->prev_pid, args->next_pid);
  }

  if (args->prev_pid == cfg->t_pid) {
    lm_switch_event_submit(LCORE_SCHED_OUT, args);
  }

  if (args->next_pid == cfg->t_pid) {
    lm_switch_event_submit(LCORE_SCHED_IN, args);
  }

  return 0;
}

static int lm_irq_event_submit(enum lcore_tid_event_type type,
                               struct trace_event_raw_irq_handler_entry* args) {
  struct lcore_tid_event* e;

  e = bpf_ringbuf_reserve(&lm_events_map, sizeof(*e), 0);
  if (!e) {
    char fmt[] = "lm event ringbuf reserve fail\n";
    bpf_trace_printk(fmt, sizeof(fmt));
    return 0;
  }

  e->type = type;
  e->ns = bpf_ktime_get_ns();
  e->irq = args->irq;

  bpf_ringbuf_submit(e, 0);
  return 0;
}

SEC("tracepoint/irq/irq_handler_entry")
int bpf_prog_irq_handler_entry(struct trace_event_raw_irq_handler_entry* args) {
  struct lcore_tid_cfg* cfg = lm_get_cfg();
  if (!cfg) return 0;

  /* core id check */
  if (bpf_get_smp_processor_id() != cfg->core_id) return 0;

  if (cfg->bpf_trace) {
    char fmt[] = "irq_handler_entry, irq %d\n";
    bpf_trace_printk(fmt, sizeof(fmt), args->irq);
  }

  lm_irq_event_submit(LCORE_IRQ_ENTRY, args);

  return 0;
}

SEC("tracepoint/irq/irq_handler_exit")
int bpf_prog_irq_handler_exit(struct trace_event_raw_irq_handler_entry* args) {
  struct lcore_tid_cfg* cfg = lm_get_cfg();
  if (!cfg) return 0;

  /* core id check */
  if (bpf_get_smp_processor_id() != cfg->core_id) return 0;

  if (cfg->bpf_trace) {
    char fmt[] = "irq_handler_exit, irq %d\n";
    bpf_trace_printk(fmt, sizeof(fmt), args->irq);
  }

  lm_irq_event_submit(LCORE_IRQ_EXIT, args);

  return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
