library(tidyverse)

benchmark_results <- read_csv("benchmark_results_Q14.csv")

benchmark_results %>%
  filter(
    page_size_power %in% c(12, 14, 16) &
      num_threads %in% c(5, 10, 20) &
      num_tuples_per_coroutine %in% c(0, 1000)
  ) %>%
  mutate(percent_cached = factor((num_cached_references * 100) %/% num_total_references)) %>% filter(percent_cached %in% c(0, 20, 40, 60, 80, 100)) %>%
  mutate(
    page_size_power = factor(page_size_power),
    num_threads = factor(num_threads)
  ) %>%
  mutate(
    page_size_power = recode(
      page_size_power,
      "12" = "Page size: 4KiB",
      "14" = "Page size: 16KiB",
      "16" = "Page size: 64KiB"
    )
  ) %>%
  mutate(
    num_threads = recode(
      num_threads,
      "1" = "1 Thread",
      "5" = "5 Threads",
      "10" = "10 Threads",
      "15" = "15 Threads",
      "20" = "20 Threads"
    )
  ) %>%
  ggplot() +
  geom_col(mapping = aes(
    x = time,
    y = percent_cached,
    fill = factor(num_entries_per_ring)
  ),
  position = "dodge") +
  facet_grid(num_threads ~ page_size_power) +
  labs(x = "Execution time (in ms)",
       y = "Fraction of index accesses that are a cache hit (in percent)",
       fill = "Kind of I/O and I/O depth per thread") +
  scale_fill_discrete(
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
