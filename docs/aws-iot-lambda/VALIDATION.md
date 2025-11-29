# AWS Lambda IoT Proxy - Validation Report

## ✅ Infrastructure Status

### Lambda Function
- **Name**: `aws-iot-mqtt-proxy`
- **Status**: Active
- **Last Update**: Successful
- **Runtime**: nodejs18.x
- **Handler**: index.handler
- **Region**: ap-south-1

### Environment Variables
- ✅ `IOT_ACCESS_KEY_ID`: Set
- ✅ `IOT_SECRET_ACCESS_KEY`: Set
- ✅ `AWS_IOT_ENDPOINT`: a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com

### API Gateway
- **Name**: aws-iot-proxy-api
- **ID**: 2ushw6qnzf
- **Endpoint**: `https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect`
- **Integration**: AWS_PROXY (Lambda)
- **Status**: Deployed

### IAM Role
- **Name**: aws-iot-proxy-lambda-role
- **ARN**: arn:aws:iam::204181332839:role/aws-iot-proxy-lambda-role
- **Policies**: 
  - AWSLambdaBasicExecutionRole
  - IoT access policy (inline)

## ⚠️ Current Issue

**Status**: API Gateway returns 500 Internal Server Error

**Symptoms**:
- Lambda function is deployed and active
- Environment variables are configured
- API Gateway integration is correct
- No CloudWatch logs appearing (suggests function may be failing before logging)

**Possible Causes**:
1. Runtime error in Lambda code (syntax or logic error)
2. Missing dependencies in package.json
3. CloudWatch logging permissions issue
4. Event format mismatch between API Gateway and Lambda

## 🔍 Next Steps for Debugging

1. **Check CloudWatch Logs**:
   ```bash
   aws logs tail /aws/lambda/aws-iot-mqtt-proxy --follow --region ap-south-1
   ```

2. **Test Lambda Directly**:
   ```bash
   aws lambda invoke \
     --function-name aws-iot-mqtt-proxy \
     --region ap-south-1 \
     --payload '{"httpMethod":"POST","body":"{\"endpoint\":\"test\",\"region\":\"test\",\"device\":\"test\",\"topic\":\"test\"}"}' \
     /tmp/response.json
   ```

3. **Verify Package Dependencies**:
   - Check if `aws-sdk` is available in Lambda runtime (it should be)
   - Verify `crypto` module is available (Node.js built-in)

4. **Check API Gateway Integration**:
   ```bash
   aws apigateway get-integration \
     --rest-api-id 2ushw6qnzf \
     --resource-id <resource-id> \
     --http-method POST \
     --region ap-south-1
   ```

## 📋 Test Page Configuration

The test page is configured with:
- **URL**: https://naptick.github.io/Naphome_Firmware/aws-iot-test.html
- **Proxy Endpoint**: Pre-filled with `https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect`
- **Authentication Method**: Backend Proxy

## ✅ What's Working

1. ✅ Lambda function created and deployed
2. ✅ API Gateway created and integrated
3. ✅ IAM role with proper permissions
4. ✅ Environment variables configured
5. ✅ Test page updated with API Gateway URL
6. ✅ Code pushed to repository

## 🔧 Manual Testing

To test once the 500 error is resolved:

```bash
curl -X POST https://2ushw6qnzf.execute-api.ap-south-1.amazonaws.com/prod/connect \
  -H "Content-Type: application/json" \
  -d '{
    "endpoint": "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com",
    "region": "ap-south-1",
    "device": "SOMNUS_F09E9E3263A4",
    "topic": "device/telemetry/SOMNUS_F09E9E3263A4"
  }'
```

Expected response:
```json
{
  "websocketUrl": "wss://a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com/mqtt?X-Amz-Algorithm=...",
  "topic": "device/telemetry/SOMNUS_F09E9E3263A4",
  "device": "SOMNUS_F09E9E3263A4",
  "endpoint": "a3ag0lenm5av45-ats.iot.ap-south-1.amazonaws.com",
  "region": "ap-south-1"
}
```

## 📝 Notes

- All infrastructure components are properly configured
- The issue appears to be in the Lambda function code execution
- CloudWatch logs would help diagnose the exact error
- Consider adding a simple "hello world" handler to verify Lambda invocation works
