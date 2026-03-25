#!/bin/bash

# Deployment script for BackTesting Engine
set -e

echo "🚀 Starting BackTesting Engine deployment..."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    print_error "Docker is not installed. Please install Docker first."
    exit 1
fi

# Check if docker-compose is installed
if ! command -v docker-compose &> /dev/null; then
    print_warning "docker-compose not found. Installing docker-compose..."
    # Install docker-compose if not present
    sudo curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" -o /usr/local/bin/docker-compose
    sudo chmod +x /usr/local/bin/docker-compose
fi

# Build the Docker image
print_status "Building Docker image..."
docker build -t backtesting-engine:latest .

if [ $? -eq 0 ]; then
    print_success "Docker image built successfully!"
else
    print_error "Failed to build Docker image"
    exit 1
fi

# Test the container locally
print_status "Testing container locally..."
docker run --rm -d --name backtesting-test -p 8080:8080 backtesting-engine:latest

# Wait for container to start
sleep 5

# Test health endpoint
if curl -f http://localhost:8080/health > /dev/null 2>&1; then
    print_success "Container is running and healthy!"
else
    print_error "Container health check failed"
    docker logs backtesting-test
    docker stop backtesting-test
    exit 1
fi

# Stop test container
docker stop backtesting-test

print_success "Local test completed successfully!"

# Ask user if they want to deploy to Render
echo ""
read -p "Do you want to deploy to Render? (y/n): " -n 1 -r
echo ""

if [[ $REPLY =~ ^[Yy]$ ]]; then
    print_status "Deploying to Render..."
    
    # Check if render CLI is installed
    if ! command -v render &> /dev/null; then
        print_warning "Render CLI not found. Please install it from: https://render.com/docs/deploy-create-a-render-account"
        print_status "You can also deploy manually by pushing to your Git repository connected to Render."
    else
        # Deploy using Render CLI
        render deploy
        print_success "Deployment initiated on Render!"
    fi
else
    print_status "Skipping Render deployment."
fi

# Show usage instructions
echo ""
print_status "Usage Instructions:"
echo "======================"
echo ""
echo "1. Local Development:"
echo "   docker-compose up"
echo ""
echo "2. Run container directly:"
echo "   docker run -p 8080:8080 backtesting-engine:latest"
echo ""
echo "3. Test the API:"
echo "   curl http://localhost:8080/health"
echo ""
echo "4. Run a backtest:"
echo "   curl -X POST http://localhost:8080/backtest \\"
echo "     -H 'Content-Type: application/json' \\"
echo "     -d '{\"symbol\":\"AAPL\",\"start_date\":\"2023-01-01\",\"end_date\":\"2023-12-31\",\"strategy\":{\"name\":\"moving_average\",\"params\":{\"short_window\":10,\"long_window\":30}}}'"
echo ""

print_success "Deployment script completed! 🎉"
