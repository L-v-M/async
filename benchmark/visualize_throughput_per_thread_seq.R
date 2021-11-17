library(tidyverse)

benchmark_results <- read_csv("benchmark_results_Q1.csv")

benchmark_results %>%
  filter(page_size_power %in% c(16) &
           do_random_io == FALSE &
           num_threads %in% c(1, 5, 10, 15, 20)) %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>% filter(percent_cached %in% c(60)) %>%
  mutate(throughput_per_thread = throughput / num_threads) %>%
  mutate(num_threads = factor(num_threads)) %>%
  select(num_threads,
         num_entries_per_ring,
         throughput_per_thread) %>%
  group_by(num_threads, num_entries_per_ring) %>%
  summarise(throughput_per_thread = median(throughput_per_thread),
            .groups = 'drop') %>%
  ggplot() +
  geom_line(mapping = aes(
    x = num_threads,
    y = throughput_per_thread,
    color = factor(num_entries_per_ring),
    group = factor(num_entries_per_ring)
  )) +
  labs(
    title = "Throughput per thread for sequential I/O",
    subtitle = "Page size of 64 KiB, 60% cached",
    x = "Number of threads",
    y = "Throughput per thread",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_color_discrete(
    limits = c("0", "2", "4", "8", "16", "32", "64"),
    labels = c(
      "Sync. (1)",
      "Async. (2)",
      "Async. (4)",
      "Async. (8)",
      "Async. (16)",
      "Async. (32)",
      "Async. (64)"
    )
  ) +
  theme(legend.position = "bottom")
