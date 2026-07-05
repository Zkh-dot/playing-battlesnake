FROM gcc:14-bookworm AS build

WORKDIR /app
COPY . /app
RUN bash tools/build_native_server.sh

FROM debian:bookworm-slim

ENV BATTLESNAKE_PORT=8000
ENV BATTLESNAKE_SEARCH_BUDGET_MS=400

WORKDIR /app
COPY --from=build /app/build/battlesnake-server /app/battlesnake-server

EXPOSE 8000

CMD ["/app/battlesnake-server"]
