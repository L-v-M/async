library(tidyverse)

results <- read_csv("/Users/leonard/Desktop/tpch_q1.csv")

results %>%
  filter(
    page_size_power == 16 &
      num_tuples_per_morsel %in% c(1000) &
      num_threads %in% c(1, 2, 4, 8, 16, 32, 64)
  ) %>%
  mutate(percent_cached = factor((num_cached_pages * 100) %/% num_total_pages)) %>%
  mutate(
    page_size_power = factor(page_size_power),
    num_threads = factor(num_threads)
  ) %>%
  mutate(page_size_power = recode(page_size_power,
                                  "16" = "Page size: 64 KiB")) %>%
  mutate(
    num_threads = recode(
      num_threads,
      "1" = "1 Thread",
      "2" = "2 Threads",
      "4" = "4 Threads",
      "8" = "8 Threads",
      "16" = "16 Threads",
      "32" = "32 Threads",
      "64" = "64 Threads"
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
  labs(x = "Fraction of pages already cached in main memory (in percent)",
       y = "Read bandwidth (in GB/s)",
       fill = "Kind of I/O and I/O depth per thread") +
  scale_fill_discrete(
    limits = c("0", "2", "4", "8", "16", "32", "64", "128", "256", "512"),
    labels = c(
      "Sync.",
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
