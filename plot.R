library(ggplot2) 

# parse logs
recv <- read.csv("rcv_packets_log.csv", header = FALSE)
names(recv) <- c("r_start", "r_end", "r_seq")
snd <- read.csv("snd_packets_log.csv", header = FALSE)
names(snd) <- c("s_start", "s_end", "s_seq")

# subtract time offset
start_time <- min(c(recv$r_start, snd$s_start))
recv$r_start <- recv$r_start - start_time
recv$r_end <- recv$r_end - start_time
snd$s_start <- snd$s_start - start_time
snd$s_end <- snd$s_end - start_time
# r_start_time <- min(recv$r_start)
# recv$r_start <- recv$r_start - r_start_time
# recv$r_end <- recv$r_end - r_start_time
# s_start_time <- min(snd$s_start)
# snd$s_start <- snd$s_start - s_start_time
# snd$s_end <- snd$s_end - s_start_time

# extract sequence number
recv$r_seq = paste0("0x", substr(recv$r_seq, 0, 2))
recv$r_seq <- bitwAnd(as.numeric(recv$r_seq), 0xF)
snd$s_seq = paste0("0x", substr(snd$s_seq, 0, 2))
snd$s_seq <- bitwAnd(as.numeric(snd$s_seq), 0xF)

# print stats
cat("rcv mean:", mean(recv$r_end - recv$r_start), "\n")
cat("rcv sdev:", sd(recv$r_end - recv$r_start), "\n")
cat("snd mean:", mean(snd$s_end - snd$s_start), "\n")
cat("snd sdev:", sd(snd$s_end - snd$s_start), "\n")

# create plot
p <- ggplot(recv)
p <- p + geom_segment(data = snd, aes(x = s_start, xend = s_end, y = s_seq-0.1, yend = s_seq-0.1), color = "blue")
p <- p + geom_point(data = snd, aes(x = s_start, y = s_seq-0.1))
p <- p + geom_point(data = snd, aes(x = s_end, y = s_seq-0.1))
p <- p + geom_segment(data = recv, aes(x = r_start, xend = r_end, y = r_seq, yend = r_seq), color = "red")
p <- p + geom_point(data = recv, aes(x = r_start, y = r_seq))
p <- p + geom_point(data = recv, aes(x = r_end, y = r_seq))
p <- p + coord_cartesian(xlim = c(000000, 12000000), expand = TRUE)
#p <- p + coord_cartesian(xlim = c(0, 18000000000), expand = TRUE)
plot(p)