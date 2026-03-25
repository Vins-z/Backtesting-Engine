#!/bin/bash

# Docker build and test script for Render deployment debugging
set -e

echo "🚀 Testing Docker build for Render deployment..."

# Clean up any existing containers/images
echo "Cleaning up existing Docker artifacts..."
docker stop backtesting-test 2>/dev/null || true
docker rm backtesting-test 2>/dev/null || true
docker rmi backtesting-engine:test 2>/dev/null || true

# Build the Docker image
echo "Building Docker image..."
docker build -t backtesting-engine:test .

echo "✅ Docker build completed successfully!"

# Test the container
echo "Testing the container..."
docker run -d --name backtesting-test -p 8080:8080 backtesting-engine:test

# Wait for container to start
echo "Waiting for container to start..."
sleep 10

# Test health endpoint
echo "Testing health endpoint..."
for i in {1..12}; do
    if curl -s http://localhost:8080/health | grep -q "healthy"; then
        echo "✅ Health check passed!"
        break
    fi
    if [ $i -eq 12 ]; then
        echo "❌ Health check failed after 60 seconds"
        echo "Container logs:"
        docker logs backtesting-test
        exit 1
    fi
    echo "Waiting for server to start... (attempt $i/12)"
    sleep 5
done

# Show container logs
echo "Container logs (last 20 lines):"
docker logs --tail 20 backtesting-test

# Clean up
echo "Cleaning up..."
docker stop backtesting-test
docker rm backtesting-test

echo "🎉 All tests passed! The Docker image should work on Render."
