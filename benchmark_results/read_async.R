library(tidyverse)

benchmark_results <- read_csv("benchmark_results_Q14.csv")

benchmark_results %>%
  filter(
    page_size_power %in% c(12) &
      num_threads %in% c(10, 20) &
      num_tuples_per_coroutine %in% c(0, 1000)
  ) %>%
  mutate(percent_cached = factor((num_cached_references * 100) %/% num_total_references)) %>% filter(percent_cached %in% c(0, 10, 20, 30, 40, 50, 60, 70, 80, 90)) %>%
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
  ggplot() +
  geom_line(mapping = aes(
    x = percent_cached,
    y = time,
    color = factor(num_entries_per_ring),
    group = factor(num_entries_per_ring)
  )) +
  facet_grid(cols = vars(num_threads_fac)) +
  labs(
    title = "Execution time for random I/O",
    subtitle = "Page size of 4 KiB",
    x = "Fraction of index accesses that are a cache hit (in percent)",
    y = "Execution time (in ms)",
    color = "Kind of I/O (I/O depth per thread)"
  ) +
  scale_fill_discrete(
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
  ) +
  theme(legend.position = "bottom")
