library(tidyverse)

benchmark_results <- read_csv("benchmark_results_Q1.csv")

benchmark_results %>%
  filter(page_size_power %in% c(16) &
           do_random_io == FALSE &
           num_threads %in% c(10, 20)) %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>% filter(percent_cached %in% c(0, 10, 20, 30, 40, 50, 60, 70, 80, 90)) %>%
  mutate(num_threads = factor(num_threads)) %>%
  mutate(
    num_threads_fac = recode(
      num_threads,
      "1" = "1 Thread",
      "5" = "5 Threads",
      "10" = "10 Threads",
      "15" = "15 Threads",
      "20" = "20 Threads"
    )
  ) %>%
  select(num_threads_fac,
         num_entries_per_ring,
         throughput,
         percent_cached) %>%
  group_by(num_threads_fac, num_entries_per_ring, percent_cached) %>%
  summarise(throughput = median(throughput),
            .groups = 'drop') %>%
  ggplot() +
  geom_line(mapping = aes(
    x = percent_cached,
    y = throughput,
    color = factor(num_entries_per_ring),
    group = factor(num_entries_per_ring)
  )) +
  facet_grid(cols = vars(num_threads_fac)) +
  labs(
    title = "Throughput per fraction of pages cached for sequential I/O",
    subtitle = "Page size of 64 KiB",
    x = "Fraction of pages already cached (in percent)",
    y = "Read bandwidth (in GB/s)",
    color = "Kind of I/O and I/O depth per thread"
  ) +
  scale_color_discrete(
    limits = c("0", "2", "4", "8", "16", "32", "64"),
    labels = c(
      "Sync.",
      "Async. (2)",
      "Async. (4)",
      "Async. (8)",
      "Async. (16)",
      "Async. (32)",
      "Async. (64)"
    )
  ) +
  theme(legend.position = "bottom")