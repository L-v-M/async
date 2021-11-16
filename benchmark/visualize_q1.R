library(tidyverse)

benchmark_results <- read_csv("benchmark_results_Q1.csv")

benchmark_results %>%
  filter(page_size_power %in% c(12, 16, 20) &
           do_random_io == FALSE &
           num_threads %in% c(5, 10, 20)) %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>% filter(percent_cached %in% c(0, 20, 40, 60, 80, 100)) %>%
  mutate(
    page_size_power = factor(page_size_power),
    num_threads = factor(num_threads)
  ) %>%
  mutate(
    page_size_power = recode(
      page_size_power,
      "12" = "Page size: 4KiB",
      "16" = "Page size: 64KiB",
      "20" = "Page size: 1MiB"
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
  geom_col(
    mapping = aes(
      x = throughput,
      y = percent_cached,
      fill = factor(num_entries_per_ring)
    ),
    position = "dodge"
  ) +
  facet_grid(num_threads ~ page_size_power) +
  labs(x = "Read bandwidth (in GB/s)",
       y = "Fraction of pages already cached in main memory (in percent)",
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
  scale_x_continuous(breaks = c(0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60)) +
  theme(legend.position = "bottom")
