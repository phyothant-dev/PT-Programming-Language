FROM gcc:13 AS builder
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends libcurl4-openssl-dev libsqlite3-dev
COPY src/ src/
COPY Makefile .
RUN make

FROM debian:bookworm-slim
WORKDIR /app
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libcurl4 libsqlite3-0 && rm -rf /var/lib/apt/lists/*
COPY --from=builder /app/pt .
COPY demo/ demo/
COPY server.pt .
EXPOSE 3000
CMD ["./pt", "server.pt"]
