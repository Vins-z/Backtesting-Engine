# Render Deployment Guide for BackTesting Engine

## 🚀 Quick Deploy to Render

### Option 1: Deploy via Render Dashboard (Recommended)

1. **Connect Your Repository**
   - Go to [Render Dashboard](https://dashboard.render.com)
   - Click "New +" → "Web Service"
   - Connect your GitHub/GitLab repository
   - Select the `cpp-backtesting-engine` directory

2. **Configure Service**
   - **Name**: `backtesting-engine`
   - **Environment**: `Docker`
   - **Region**: `Oregon` (or your preferred region)
   - **Branch**: `main`
   - **Root Directory**: `cpp-backtesting-engine` (if deploying from monorepo)

3. **Environment Variables**
   ```
   NODE_ENV=production
   LOG_LEVEL=info
   PORT=8080
   RENDER=true
   ```

4. **Deploy**
   - Click "Create Web Service"
   - Render will automatically build and deploy your service

### Option 2: Deploy via render.yaml (Blue-Green Deployment)

The `render.yaml` file is already configured for automatic deployment:

```yaml
services:
  - type: web
    name: backtesting-engine
    env: docker
    region: oregon
    plan: free
    dockerfilePath: ./Dockerfile
    healthCheckPath: /health
    autoDeploy: true
```

## 🔧 Configuration Details

### Health Check Endpoint
- **Path**: `/health`
- **Expected Response**: `{"status":"healthy"}`
- **Check Interval**: 30 seconds

### Port Configuration
- **Default Port**: 8080
- **Environment Variable**: `PORT`
- **Render Auto-Detection**: Yes

### Build Process
1. **Docker Build**: Multi-stage build for optimized image size
2. **Dependencies**: All C++ libraries and TA-Lib included
3. **Security**: Non-root user execution
4. **Optimization**: Release build with C++20 standard

## 📊 Monitoring & Logs

### View Logs
```bash
# Via Render Dashboard
Dashboard → Your Service → Logs

# Via Render CLI
render logs backtesting-engine
```

### Health Monitoring
- **Automatic Health Checks**: Every 30 seconds
- **Startup Time**: ~60 seconds (includes TA-Lib compilation)
- **Memory Usage**: ~256MB-512MB
- **CPU Usage**: Optimized for free tier

## 🔍 Testing Your Deployment

### Health Check
```bash
curl https://your-service-name.onrender.com/health
```

### Market Data API
```bash
# Get all market data
curl https://your-service-name.onrender.com/api/market-data

# Get specific symbol data (fetches real-time data from Alpha Vantage)
curl https://your-service-name.onrender.com/api/market-data/AAPL

# Get specific symbol data (NKE example)
curl https://your-service-name.onrender.com/api/market-data/NKE
```

### Backtest API
```bash
curl -X POST https://your-service-name.onrender.com/backtest \
  -H 'Content-Type: application/json' \
  -d '{
    "symbol": "AAPL",
    "start_date": "2023-01-01",
    "end_date": "2023-12-31",
    "initial_capital": 10000,
    "strategy": {
      "indicators": [
        {
          "type": "SMA",
          "length": 20
        }
      ]
    }
  }'
```

## 🛠️ Troubleshooting

### Common Issues

1. **Build Fails - TA-Lib Compilation**
   ```
   Solution: The Dockerfile includes TA-Lib compilation. 
   This may take 2-3 minutes on first build.
   ```

2. **Health Check Fails**
   ```
   Solution: Check logs for server startup issues.
   The server needs ~60 seconds to start.
   ```

3. **Memory Issues**
   ```
   Solution: Free tier has 512MB limit.
   The service is optimized for this constraint.
   ```

4. **Port Issues**
   ```
   Solution: Render automatically sets PORT environment variable.
   The server listens on the correct port.
   ```

### Debug Commands

```bash
# Check service status
render ps backtesting-engine

# View recent logs
render logs backtesting-engine --tail 100

# Restart service
render restart backtesting-engine
```

## 🔄 Continuous Deployment

### Automatic Deployments
- **Trigger**: Push to `main` branch
- **Build**: Automatic Docker build
- **Deploy**: Blue-green deployment
- **Rollback**: Available via dashboard

### Manual Deployments
```bash
# Deploy specific branch
render deploy backtesting-engine --branch feature/new-strategy

# Deploy with custom environment
render deploy backtesting-engine --env-var LOG_LEVEL=debug
```

## 📈 Scaling

### Free Tier Limits
- **Memory**: 512MB
- **CPU**: Shared
- **Bandwidth**: 100GB/month
- **Build Time**: 15 minutes

### Paid Tier Benefits
- **Memory**: Up to 32GB
- **CPU**: Dedicated cores
- **Custom Domains**: Yes
- **SSL Certificates**: Automatic

## 🔐 Security

### Built-in Security Features
- **Non-root User**: Service runs as `backtest` user
- **Read-only Filesystem**: Except for `/app/data`
- **Minimal Dependencies**: Only runtime libraries
- **Health Checks**: Automatic monitoring

### Environment Variables
```bash
# Production (required for production deployments)
NODE_ENV=production
LOG_LEVEL=info

# Development (never use in production)
NODE_ENV=development
LOG_LEVEL=debug
```
**Important:** Always set `NODE_ENV=production` for production deployments. Using development mode in production can expose sensitive information and reduce performance.

## 📝 API Documentation

### Endpoints

#### Health Check
```
GET /health
Response: {"status": "healthy"}
```

#### Market Data
```
GET /api/market-data/{symbol}
Response: Real-time market data from Alpha Vantage API

Example:
GET /api/market-data/AAPL
Response:
{
  "status": "success",
  "symbol": "AAPL",
  "timestamp": "1703123456",
  "data": {
    "symbol": "AAPL",
    "price": "151.45",
    "change": "1.20",
    "change_percent": "0.80%",
    "volume": "45678900",
    "high": "152.30",
    "low": "149.80",
    "open": "150.25",
    "previous_close": "150.25"
  }
}
```

#### Backtest
```
POST /backtest
Content-Type: application/json

Request Body:
{
  "symbol": "AAPL",
  "start_date": "2023-01-01",
  "end_date": "2023-12-31",
  "initial_capital": 10000,
  "strategy": {
    "indicators": [
      {
        "type": "SMA",
        "length": 20
      }
    ]
  }
}

Response:
{
  "status": "success",
  "message": "Backtest completed",
  "data": {
    "total_return": 0.15,
    "sharpe_ratio": 1.2,
    "max_drawdown": 0.08,
    "total_trades": 45,
    "win_rate": 0.65,
    "profit_factor": 1.8,
    "trades": [...],
    "equity_curve": [...]
  },
  "timestamp": "1703123456"
}
```

## 🎯 Performance Optimization

### Build Optimizations
- **Multi-stage Build**: Reduces final image size
- **Layer Caching**: Faster rebuilds
- **Dependency Caching**: TA-Lib compiled once

### Runtime Optimizations
- **C++20 Standard**: Latest optimizations
- **Release Build**: Maximum performance
- **Memory Management**: Efficient allocation
- **Concurrent Processing**: Multi-threaded backtesting

## 📞 Support

### Render Support
- **Documentation**: [render.com/docs](https://render.com/docs)
- **Community**: [render.com/community](https://render.com/community)
- **Status**: [status.render.com](https://status.render.com)

### BackTesting Engine Support
- **Issues**: GitHub repository issues
- **Documentation**: README.md in repository
- **Logs**: Available in Render dashboard

## ✅ Working API Endpoints

The following endpoints are now fully functional and tested:

### Market Data Endpoints
- ✅ `GET /api/market-data` - Get all cached market data
- ✅ `GET /api/market-data/{symbol}` - Get real-time data for specific symbol (e.g., NKE, AAPL)
- ✅ `POST /api/market-data/update` - Manually update symbol data
- ✅ `GET /api/market-data/history` - Get available symbols

### Backtest Endpoints
- ✅ `POST /backtest` - Run actual backtests with real data (no more hardcoded responses)
- ✅ `POST /api/strategy/parse` - Parse natural language strategies
- ✅ `POST /api/strategy/backtest` - Run backtests from natural language

### System Endpoints
- ✅ `GET /health` - Health check
- ✅ `GET /` - API documentation page

### Key Improvements Made
1. **Real Alpha Vantage Integration**: Market data endpoints now fetch real-time data from Alpha Vantage API
2. **Actual Backtest Execution**: Backtest endpoint now runs real backtests using the BacktestEngine
3. **Proper Error Handling**: All endpoints return meaningful error messages
4. **Rate Limiting**: Proper rate limiting for Alpha Vantage API (2 requests/minute with 30-second intervals)
5. **Caching**: Intelligent caching system for market data with automatic updates every 30 seconds

### Test Commands
```bash
# Test market data (should work now)
curl https://backtesting-2w2g.onrender.com/api/market-data/NKE

# Test backtest (now runs real backtests)
curl -X POST https://backtesting-2w2g.onrender.com/backtest \
  -H 'Content-Type: application/json' \
  -d '{
    "symbol": "AAPL",
    "start_date": "2024-01-01",
    "end_date": "2024-12-31",
    "initial_capital": 10000,
    "strategy": {
      "indicators": [{ "type": "SMA", "length": 20 }]
    }
  }'
```

---

**Deployment Status**: ✅ Ready for Render deployment
**API Status**: ✅ All endpoints working with real data
**Estimated Build Time**: 3-5 minutes (first time)
**Estimated Startup Time**: 60 seconds
**Memory Usage**: 256-512MB
**API Endpoint**: `https://your-service-name.onrender.com`
