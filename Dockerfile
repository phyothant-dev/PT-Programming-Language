# Build stage: compile PT from source
FROM gcc:13 AS builder
WORKDIR /app
COPY src/ src/
COPY Makefile .
RUN make

# Runtime stage: slim image with just the binary
FROM debian:bookworm-slim
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/pt .
COPY demo/ demo/
COPY server.pt .
EXPOSE 3000
CMD ["./pt", "server.pt"]
