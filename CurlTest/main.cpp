#include <curl/curl.h>
#include <curl/easy.h>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Progress.H>

#include <iostream>
#include <chrono>
#include <thread>
#include <regex>
#include <mutex>
#include <vector>
#include <string>

size_t writeFunc(void* data, size_t size, size_t nmemb, void* userp)
{
	std::string* str = static_cast<std::string*>(userp);

	str->append(static_cast<char*>(data), nmemb);

	return nmemb;
}

class PollDestroyerWindow : public Fl_Window
{
	Fl_Input* pollNumberInput;
	Fl_Input* pollOptionInput;
	Fl_Input* pollVoteNumberInput;
	Fl_Button* startButton;
	Fl_Button* stopButton;

	Fl_Progress* pollProgressBar;

	std::thread startButtonThread;
	std::atomic_bool close = false;

	void voteFunc(int poll, int option, int numvotes)
	{
		//first we get the html file to get the nonce

		auto easyhandle = curl_easy_init();

		std::string htmlFile;
		curl_easy_setopt(easyhandle, CURLOPT_WRITEFUNCTION, writeFunc);
		curl_easy_setopt(easyhandle, CURLOPT_WRITEDATA, &htmlFile);

		curl_easy_setopt(easyhandle, CURLOPT_URL, "https://thetalononline.org");

		curl_easy_perform(easyhandle);

		//we find the nonce using a regex
		std::string nonce;
		{
			auto start = "<p style=\"display: none;\"><input type=\"hidden\" id=\"poll_" + std::to_string(poll) + "_nonce\" name=\"wp-polls-nonce\" value=\"";

			auto end = std::string{ "\" /></p>" };

			std::regex findRegex{ start + "(.*)" + end };

			std::smatch findMatch;

			if (std::regex_search(htmlFile, findMatch, findRegex))
			{
				if (findMatch.size() == 2)
				{
					nonce = findMatch[1].str();
				}
			}
			else
			{
				std::cerr << "Could not find nonce using regex\n";
				return;
			}
		}

		//next we send the vote requests
		//reset options
		curl_easy_reset(easyhandle);

		std::string postRequestStr = std::string{ "action=polls&view=process&poll_id=" } + std::to_string(poll) + "&poll_" + std::to_string(poll) + "=" + std::to_string(option) + "&poll_" + std::to_string(poll) + "_nonce=" + nonce;

		curl_easy_setopt(easyhandle, CURLOPT_POSTFIELDS, postRequestStr.c_str());
		curl_easy_setopt(easyhandle, CURLOPT_URL, "https://thetalononline.org/wp-admin/admin-ajax.php");
		curl_easy_setopt(easyhandle, CURLOPT_POST, 1);


		Fl::lock();
		pollProgressBar->selection_color(FL_YELLOW);
		Fl::unlock();
		Fl::awake();

		for (int i = 0; i < numvotes; i++)
		{
			curl_easy_perform(easyhandle);
			Fl::lock();
			pollProgressBar->value(static_cast<float>(i) / static_cast<float>(numvotes) * 100.0f);
			Fl::unlock();
			Fl::awake();
			if (close.load())
			{
				close.store(false);
				break;
			}
		}

		Fl::lock();
		pollProgressBar->value(100.0f);
		pollProgressBar->selection_color(FL_GREEN);
		Fl::unlock();
		Fl::awake();

		curl_easy_cleanup(easyhandle);
	}

	static void startButtonCallback(Fl_Widget* widget, void* data)
	{
		//remember that japan city is poll 157, and 781
		//poll_nonce for taht is "e6d1fedae8"
		//ajax url is "https://thetalononline.org/wp-admin/admin-ajax.php"

		auto window = static_cast<PollDestroyerWindow*>(data);

		int pollNumber, pollOption, pollNumVotes;
		try
		{
			Fl::lock();
			pollNumber = std::stoi(window->pollNumberInput->value());
			Fl::unlock();
			Fl::awake();
		}
		catch (...)
		{
			std::cerr << "Failed to convert Poll Number into an integer\n";
			Fl::unlock();
			Fl::awake();
			return;
		}
		try
		{
			Fl::lock();
			pollOption = std::stoi(window->pollOptionInput->value());
			Fl::unlock();
			Fl::awake();
		}
		catch (...)
		{
			std::cerr << "Failed to convert Poll Option into an integer\n";
			Fl::unlock();
			Fl::awake();
			return;
		}
		try
		{
			Fl::lock();
			pollNumVotes = std::stoi(window->pollVoteNumberInput->value());
			Fl::unlock();
			Fl::awake();
		}
		catch (...)
		{
			std::cerr << "Failed to convert Number of Votes into an integer\n";
			Fl::unlock();
			Fl::awake();
			return;
		}

		if (window->startButtonThread.joinable())
		{
			window->startButtonThread.join();
		}
		Fl::lock();
		window->pollProgressBar->value(0.0f);
		Fl::unlock();
		Fl::awake();

		window->startButtonThread = std::thread{ &PollDestroyerWindow::voteFunc, window, pollNumber, pollOption, pollNumVotes };
	}

	static void endButtonCallback(Fl_Widget* widget, void* data)
	{
		auto window = static_cast<PollDestroyerWindow*>(data);

		window->close.store(true);
	}

public:
	PollDestroyerWindow()
		: Fl_Window(200, 125, "Poll Destroyer")
	{
		color(FL_WHITE);

		begin();

		pollNumberInput = new Fl_Input(130, 0, 70, 25, "Poll Number: ");
		pollNumberInput->labelsize(13);

		pollOptionInput = new Fl_Input(130, 25, 70, 25, "Poll Option: ");
		pollOptionInput->labelsize(13);

		pollVoteNumberInput = new Fl_Input(130, 50, 70, 25, "Number of Votes: ");
		pollVoteNumberInput->labelsize(13);

		startButton = new Fl_Button(0, 75, 100, 25, "Start!");
		startButton->labelsize(18);
		startButton->callback(startButtonCallback, this);

		stopButton = new Fl_Button(100, 75, 100, 25, "Stop.");
		stopButton->labelsize(18);
		stopButton->callback(endButtonCallback, this);

		pollProgressBar = new Fl_Progress(0, 100, 200, 25, "Poll Progress");
		pollProgressBar->labelsize(18);
		pollProgressBar->maximum(100.0f);
		pollProgressBar->minimum(0.0f);
		pollProgressBar->value(0.0f);
		pollProgressBar->selection_color(FL_YELLOW);

		resizable(this);

		end();

		Fl::lock();

		show();
	}

	~PollDestroyerWindow()
	{
		if (startButtonThread.joinable())
		{
			startButtonThread.join();
		}
	}
};

int main(int argc, char** argv)
{
	PollDestroyerWindow window;

	return Fl::run();
}