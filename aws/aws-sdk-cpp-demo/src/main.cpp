#include <aws/core/Aws.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/utils/logging/LogLevel.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/model/HeadObjectRequest.h>

#include <iostream>
#include <string>

using namespace Aws;
using namespace Aws::S3;
using namespace Aws::S3::Model;

bool ParseS3Uri(const String& uri, String& bucket, String& key) {
    const String prefix = "s3://";
    if (uri.find(prefix) != 0) return false;

    size_t bucket_start = prefix.length();
    size_t bucket_end = uri.find('/', bucket_start);

    if (bucket_end == String::npos) return false;

    bucket = uri.substr(bucket_start, bucket_end - bucket_start);
    key = uri.substr(bucket_end + 1);

    return !bucket.empty() && !key.empty();
}

int main(int argc, char** argv) {
    SDKOptions options;
    options.loggingOptions.logLevel = Utils::Logging::LogLevel::Info;
    InitAPI(options);

    {
        Client::ClientConfiguration config;

        Auth::AWSCredentials credentials;
        credentials.SetAWSAccessKeyId("YOUR_ACCESS_KEY_ID");
        credentials.SetAWSSecretKey("YOUR_SECRET_ACCESS_KEY");
        config.region = "REGION";             // Like ap-southeast-2
        config.endpointOverride = "ENDPOINT"; // Like s3.ap-southeast-2.amazonaws.com

        // Proxy settings
        // config.proxyHost = "10.8.6.103";
        // config.proxyPort = 3128;
        // config.proxyScheme = Http::Scheme::HTTP;

        S3Client s3_client(credentials, nullptr, config);

        String s3Uri = "YOUR_S3_FILE_PATH";
        String bucket, key;

        if (!ParseS3Uri(s3Uri, bucket, key)) {
            std::cerr << "Invalid S3 URI format. Expected s3://bucket/key" << std::endl;
            return 1;
        }
        std::cout << "Target Bucket: " << bucket << std::endl;
        std::cout << "Target Key   : " << key << std::endl;

        // Create the Metadata request
        HeadObjectRequest request;
        request.SetBucket(bucket);
        request.SetKey(key);

        auto outcome = s3_client.HeadObject(request);

        if (outcome.IsSuccess()) {
            long long fileSize = outcome.GetResult().GetContentLength();
            std::cout << "\n[Success]" << std::endl;
            std::cout << "File Size: " << fileSize << " bytes (" << fileSize / (1024.0 * 1024.0) << " MB)" << std::endl;
            std::cout << "ETag     : " << outcome.GetResult().GetETag() << std::endl;
        } else {
            const auto& error = outcome.GetError();
            std::cerr << "\n[Failed]" << std::endl;
            std::cerr << "Error Type: " << error.GetExceptionName() << std::endl;
            std::cerr << "Message   : " << error.GetMessage() << std::endl;
        }
    }

    ShutdownAPI(options);
    return 0;
}
