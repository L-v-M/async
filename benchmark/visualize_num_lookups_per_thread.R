library(tidyverse)

benchmark_results <- read_csv("benchmark_results_Q14.csv")

benchmark_results %>%
  filter(
    page_size_power %in% c(12) &
      num_threads %in% c(1, 5, 10, 15, 20) &
      num_tuples_per_coroutine %in% c(0, 1000)
  ) %>%
  mutate(percent_cached = factor((num_cached_references * 100) %/% num_total_references)) %>% filter(percent_cached %in% c(60)) %>%
  mutate(lookups_per_second = num_total_references / (time / 1000)) %>%
  mutate(lookups_per_second_per_thread = lookups_per_second / num_threads) %>%
  group_by(num_threads, num_entries_per_ring) %>%
  summarise(
    lookups_per_second_per_thread = median(lookups_per_second_per_thread),
    .groups = 'drop'
  ) %>%
  ggplot() +
  geom_line(
    mapping = aes(
      x = num_threads,
      y = lookups_per_second_per_thread,
      color = factor(num_entries_per_ring),
      group = factor(num_entries_per_ring)
    )
  ) +
  labs(
    title = "Lookups per second per thread for random I/O",
    subtitle = "Page size of 4 KiB, 60% cached",
    x = "Number of threads",
    y = "Number of lookups per second per thread",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_color_discrete(
    limits = c("0", "2", "4", "8", "16", "32", "64"),
    labels = c(
      "Sync. (0)",
      "Async. (2)",
      "Async. (4)",
      "Async. (8)",
      "Async. (16)",
      "Async. (32)",
      "Async. (64)"
    )
  )
