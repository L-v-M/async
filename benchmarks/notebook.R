# Overview

## TPC-H Q1

```{r}
library(tidyverse)
results <- read_csv("/Users/leonard/Desktop/tpch_q1.csv")
results %>%
  filter(page_size_power == 16) %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>%
  select(num_threads,
         percent_cached,
         num_entries_per_ring,
         throughput) %>%
  group_by(num_threads, percent_cached, num_entries_per_ring) %>%
  summarise(throughput = max(throughput),
            .groups = 'drop') %>%
  mutate(
    num_threads = recode(
      factor(num_threads),
      "1" = "1 Thread",
      "2" = "2 Threads",
      "4" = "4 Threads",
      "8" = "8 Threads",
      "16" = "16 Threads",
      "32" = "32 Threads",
      "64" = "64 Threads",
      "128" = "128 Threads"
    )
  ) %>%
  ggplot() +
  geom_col(
    mapping = aes(
      x = percent_cached,
      y = throughput,
      fill = factor(num_entries_per_ring)
    ),
    position = "dodge"
  ) +
  facet_grid(cols = vars(num_threads)) +
  labs(x = "Fraction of pages cached in DRAM (in %)",
       y = "Bandwidth of processing TPC-H Q1 (in GB/s)",
       fill = "Kind of I/O (I/O depth per thread)") +
  scale_fill_discrete(
    limits = c("0", "2", "4", "8", "16", "32", "64", "128", "256", "512"),
    labels = c(
      "Sync.  (1)",
      "Async. (2)",
      "Async. (4)",
      "Async. (8)",
      "Async. (16)",
      "Async. (32)",
      "Async. (64)",
      "Async. (128)",
      "Async. (256)",
      "Async. (512)"
    )
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 30)) +
  theme(legend.position = "bottom")
```

## TPC-H Q14

```{r}
library(tidyverse)
results <- read_csv("/Users/leonard/Desktop/tpch_q14.csv")
results %>%
  filter(num_cached_references < num_total_references) %>%
  mutate(percent_cached = factor((num_cached_references * 100) %/% num_total_references)) %>%
  mutate(lookups_per_second = num_total_references / (time / 1000)) %>%
  select(num_threads,
         percent_cached,
         num_entries_per_ring,
         lookups_per_second) %>%
  group_by(num_threads, percent_cached, num_entries_per_ring) %>%
  summarise(lookups_per_second = max(lookups_per_second),
            .groups = 'drop') %>%
  mutate(
    num_threads = recode(
      factor(num_threads),
      "1" = "1 Thread",
      "2" = "2 Threads",
      "4" = "4 Threads",
      "8" = "8 Threads",
      "16" = "16 Threads",
      "32" = "32 Threads",
      "64" = "64 Threads",
      "128" = "128 Threads"
    )
  ) %>%
  ggplot() +
  geom_col(
    mapping = aes(
      x = percent_cached,
      y = lookups_per_second,
      fill = factor(num_entries_per_ring)
    ),
    position = "dodge"
  ) +
  facet_grid(cols = vars(num_threads)) +
  labs(x = "Fraction of index accesses that are a cache hit (in %)",
       y = "Number of lookups per second",
       fill = "Kind of I/O (I/O depth per thread)") +
  scale_fill_discrete(
    limits = c("0", "8", "16", "32", "64", "128", "256", "512"),
    labels = c(
      "Sync. (1)",
      "Async. (8)",
      "Async. (16)",
      "Async. (32)",
      "Async. (64)",
      "Async. (128)",
      "Async. (256)",
      "Async. (512)"
    )
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 40)) +
  theme(legend.position = "bottom")
```

# Get higher throughput with fewer threads

## TPC-H Q1

```{r}
library(tidyverse)
results <- read_csv("/Users/leonard/Desktop/tpch_q1.csv")
results %>%
  filter(page_size_power == 16 &
           num_entries_per_ring %in% c(0, 4, 64, 128)) %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>%
  filter(percent_cached == 60) %>%
  mutate(throughput_per_thread = throughput / num_threads) %>%
  select(num_threads,
         num_entries_per_ring,
         throughput_per_thread) %>%
  group_by(num_threads, num_entries_per_ring) %>%
  summarise(throughput_per_thread = max(throughput_per_thread),
            .groups = 'drop') %>%
  ggplot() +
  geom_line(mapping = aes(
    x = factor(num_threads),
    y = throughput_per_thread,
    color = factor(num_entries_per_ring),
    group = factor(num_entries_per_ring)
  )) +
  labs(
    title = "Bandwidth per thread of processing TPC-H Q1",
    subtitle = "Page size of 64 KiB, 60% cached",
    x = "Number of threads",
    y = "Bandwidth per thread (in GB/s)",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_color_discrete(
    limits = c("0", "4", "64", "128"),
    labels = c(
      "Synchronous (1)",
      "Asynchronous (4)",
      "Asynchronous (64)",
      "Asynchronous (128)"
    )
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 10)) 
```

## TPC-H Q14

```{r}
library(tidyverse)
results <- read_csv("/Users/leonard/Desktop/tpch_q14.csv")
results %>%
  filter(num_entries_per_ring %in% c(0, 8, 32, 64, 256, 512)) %>%
  mutate(percent_cached = factor((num_cached_references * 100) %/% num_total_references)) %>%
  filter(percent_cached %in% c(60)) %>%
  mutate(lookups_per_second = num_total_references / (time / 1000)) %>%
  mutate(lookups_per_second_per_thread = lookups_per_second / num_threads) %>%
  select(num_threads,
         num_entries_per_ring,
         lookups_per_second_per_thread) %>%
  group_by(num_threads, num_entries_per_ring) %>%
  summarise(
    lookups_per_second_per_thread = max(lookups_per_second_per_thread),
    .groups = 'drop'
  ) %>%
  ggplot() +
  geom_line(
    mapping = aes(
      x = factor(num_threads),
      y = lookups_per_second_per_thread,
      color = factor(num_entries_per_ring),
      group = factor(num_entries_per_ring)
    )
  ) +
  labs(
    title = "Lookups per second per thread of processing TPC-H Q14",
    subtitle = "Page size of 4 KiB, 60% cached",
    x = "Number of threads",
    y = "Number of lookups per second per thread",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_color_discrete(
    limits = c("0", "8", "32", "64", "256", "512"),
    labels = c(
      "Sync. (1)",
      "Async. (8)",
      "Async. (32)",
      "Async. (64)",
      "Async. (256)",
      "Async. (512)"
    )
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 15))
```

# A lot of data must be cached for sequential I/O to catch up with random I/O

```{r}
library(tidyverse)
results <- read_csv("/Users/leonard/Desktop/tpch_q1.csv") %>%
  filter(page_size_power == 16 &
           num_threads == 8 &
           num_entries_per_ring %in% c(0, 128))

y_intercept <- results %>%
  filter(num_cached_pages == 0 &
           num_entries_per_ring == 128) %>%
  group_by(num_entries_per_ring) %>%
  summarise(throughput = max(throughput),
            .groups = 'drop') %>%
  pull(throughput)

results %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>%
  select(percent_cached,
         num_entries_per_ring,
         throughput) %>%
  group_by(percent_cached, num_entries_per_ring) %>%
  summarise(throughput = max(throughput),
            .groups = 'drop') %>%
  ggplot() +
  geom_line(mapping = aes(
    x = percent_cached,
    y = throughput,
    color = factor(num_entries_per_ring),
    group = factor(num_entries_per_ring)
  )) +
  geom_hline(yintercept = y_intercept) +
  labs(
    title = "Bandwidth of processing TPC-H Q1",
    subtitle = "Page size of 64 KiB, 8 threads",
    x = "Fraction of pages cached in main memory (in %)",
    y = "Bandwidth (in GB/s)",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_color_discrete(
    limits = c("0", "128"),
    labels = c("Synchronous (1)",
               "Asynchronous (128)")
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 20))
```

# You need fewer threads to get max ssd bandwidth than dram bandwidth

```{r}
library(tidyverse)
results <- read_csv("/Users/leonard/Desktop/tpch_q1.csv") %>%
  filter(page_size_power == 16 &
           num_threads == 32 &
           num_entries_per_ring %in% c(0, 64))

y_intercept <- results %>%
  filter(num_cached_pages == 0 &
           num_entries_per_ring == 64) %>%
  group_by(num_entries_per_ring) %>%
  summarise(throughput = max(throughput),
            .groups = 'drop') %>%
  pull(throughput)

results %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>%
  select(percent_cached,
         num_entries_per_ring,
         throughput) %>%
  group_by(percent_cached, num_entries_per_ring) %>%
  summarise(throughput = max(throughput),
            .groups = 'drop') %>%
  ggplot() +
  geom_line(mapping = aes(
    x = percent_cached,
    y = throughput,
    color = factor(num_entries_per_ring),
    group = factor(num_entries_per_ring)
  )) +
  geom_hline(yintercept = y_intercept) +
  labs(
    title = "Bandwidth of processing TPC-H Q1",
    subtitle = "Page size of 64 KiB, 32 threads",
    x = "Fraction of pages cached in main memory (in %)",
    y = "Bandwidth (in GB/s)",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_color_discrete(
    limits = c("0", "64"),
    labels = c("Synchronous (1)",
               "Asynchronous (64)")
  ) +
  scale_y_continuous(breaks = scales::pretty_breaks(n = 30))
```