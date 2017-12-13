#include <so_5/all.hpp>

#include <optional>

using namespace std::chrono;
using clock_type = steady_clock;

class anti_jitter_mbox : public so_5::abstract_message_box_t {
	// Вспомогательный тип для хранения информации о типах сообщений, на которые
	// есть подписки и для которых нужно хранить время поступления последнего
	// сообщения.
	struct data {
		// Элемент данных для одного типа сообщения.
		struct item {
			// Количество подписок на этот тип сообщения. Значение 0 означает,
			// что подписок нет и больше это сообщение можно не контролировать.
			std::size_t subscribers_{0};
			// Время поступления последнего сообщения.
			// Пустое значение указывает, что сообщение еще ни разу не поступало.
			std::optional<clock_type::time_point> last_received_{};
		};

		// Тип ассоциативного контейнера, который потребуется для хранения
		// информации о сообщениях.
		using message_table = std::map<std::type_index, item>;

		// Нам потребуется mutex для защиты содержимого mbox-а в многопоточном
		// окружении.
		std::mutex lock_;
		// Таблица сообщений, на которые есть подписки.
		message_table messages_;
	};

	// Содержимое mbox-а.

	// Актуальный mbox, через который доставка сообщений и будет выполняться.
	const so_5::mbox_t mbox_;
	// Минимальный порог для отсечения "лишних" сообщений.
	const clock_type::duration timeout_;
	// Собственные данные mbox-а. В SObjectizer-5.5 должны быть помечены
	// как mutable, т.к. их придется модифицировать в том числе и в
	// const-методах.
	mutable data data_;

	// Вспомогательный метод, который захватывает замок объекта и
	// выполняет указанную лямбду под этим замком.
	// Так же в случае возникновения исключения прерывает работу всего
	// приложения (для простоты exception safety обеспечивается вот
	// таким суровым образом).
	template<typename Lambda>
	decltype(auto) lock_and_perform(Lambda l) const noexcept {
		std::lock_guard<std::mutex> lock{data_.lock_};
		return l();
	}

public:
	// Конструктор. Конструктору требуется реальный MPSC-mbox, которому
	// и будет делегироваться актуальная доставка сообщения.
	anti_jitter_mbox(
		so_5::mbox_t actual_mbox,
		clock_type::duration timeout)
		: mbox_{std::move(actual_mbox)}
		, timeout_{timeout}
	{}

	// Уникальный ID mbox-а. Используем для этих целей ID актуального mbox-а.
	so_5::mbox_id_t id() const override { return mbox_->id(); }

	// Обработка регистрации очередного подписчика.
	void subscribe_event_handler(
			const std::type_index & msg_type,
			const so_5::message_limit::control_block_t * limit,
			so_5::agent_t * subscriber ) override {
		lock_and_perform([&]{
			// Достаем информацию о сообщениях этого типа. Если такой информации
			// еще не было, то она будет создана автоматически.
			auto & msg_data = data_.messages_[msg_type];
			msg_data.subscribers_ += 1;

			// Дальнейшую работу делегируем актуальному mbox-у.
			mbox_->subscribe_event_handler(msg_type, limit, subscriber);
		});
	}

	// Обработка дерегистрации подписчика.
	void unsubscribe_event_handlers(
			const std::type_index & msg_type,
			so_5::agent_t * subscriber ) override {
		lock_and_perform([&]{
			// Достаем информацию о сообщениях данного типа.
			// Если таковой не окажется, то ничего делать не нужно.
			auto it = data_.messages_.find(msg_type);
			if(it != data_.messages_.end()) {
				auto & msg_data = it->second;
				--msg_data.subscribers_;
				if(!msg_data.subscribers_)
					// Подписчиков больше не осталось, поэтому информацию о
					// сообщениях этого типа хранить больше не нужно.
					data_.messages_.erase(it);

				// Актуальный mbox так же должен выполнить свою работу.
				mbox_->unsubscribe_event_handlers(msg_type, subscriber);
			}
		});
	}

	// Уникальное имя mbox-а.
	std::string query_name() const override {
		return "<mbox:type=anti-jitter-mpsc:id=" + std::to_string(id()) + ">";
	}

	// Тип нашего mbox-а. Такой же, как и у актуального.
	so_5::mbox_type_t type() const override {
		return mbox_->type();
	}

	// Обработка попытки доставки обычного сообщения.
	void do_deliver_message(
			const std::type_index & msg_type,
			const so_5::message_ref_t & message,
			unsigned int overlimit_reaction_deep ) const override {
		lock_and_perform([&]{
			// Нужно найти информацию об этом типе сообщений.
			// Если тип нам неизвестен, значит подписчиков нет и сообщение
			// доставлять никуда не нужно.
			auto it = data_.messages_.find(msg_type);
			if(it != data_.messages_.end()) {
				auto & msg_data = it->second;
				const auto now = clock_type::now();

				// Проверяем, нужно ли доставлять сообщение.
				// Оно приходит к нам впервые (т.е. значения last_received_
				// еще нет), то доставлять нужно.
				bool should_be_delivered = true;
				if(msg_data.last_received_) {
					should_be_delivered = (now - *(msg_data.last_received_)) >= timeout_;
				}

				// Если все-таки нужно, то доставляем через актуальный mbox и
				// обновляем информацию о времени последней доставки этого
				// сообщения.
				if(should_be_delivered) {
					msg_data.last_received_ = now;
					mbox_->do_deliver_message(msg_type, message, overlimit_reaction_deep);
				}
			}
		});
	}

	// Доставку синхронных запросов запрещаем.
	void do_deliver_service_request(
			const std::type_index & /*msg_type*/,
			const so_5::message_ref_t & /*message*/,
			unsigned int /*overlimit_reaction_deep*/ ) const override {
		// Для того, чтобы выбростить исключение so_5::exception_t и сделать
		// это просто, используем соответствующий макрос из SObjectizer-а.
		SO_5_THROW_EXCEPTION(so_5::rc_not_implemented,
				"anti-jitter-mbox doesn't support service requests");
	}

	// Фильтры доставки для MPSC-mbox-ов не работают. Поэтому сразу
	// порождаем соответствующее исключение.
	void set_delivery_filter(
			const std::type_index & /*msg_type*/,
			const so_5::delivery_filter_t & /*filter*/,
			so_5::agent_t & /*subscriber*/ ) override {
		SO_5_THROW_EXCEPTION(so_5::rc_not_implemented,
				"anti-jitter-mbox doesn't support delivery filters");
	}

	void drop_delivery_filter(
			const std::type_index & /*msg_type*/,
			so_5::agent_t & /*subscriber*/ ) noexcept override {
		SO_5_THROW_EXCEPTION(so_5::rc_not_implemented,
				"anti-jitter-mbox doesn't support delivery filters");
	}
};

// Агент, который будет использовать свой обычный MPSC-mbox для получения
// сообщений.
class ordinary_subscriber final : public so_5::agent_t {
	const std::string name_;
public:
	ordinary_subscriber(context_t ctx,
		// Уникальное имя, которое будет использовать агент.
		std::string name)
		: so_5::agent_t{std::move(ctx)}
		, name_{std::move(name)}
	{
		so_subscribe_self().event([&](mhood_t<std::string> cmd) {
			std::cout << name_ << ": signal received -> " << *cmd << std::endl;
		});
	}

	// Mbox, который должен использоваться для отсылки сообщений.
	auto target_mbox() const { return so_direct_mbox(); }
};

// Агент, который будет использовать anti-jitter-mbox для получения сообщений.
class anti_jitter_subscriber final : public so_5::agent_t {
	const std::string name_;
	const so_5::mbox_t anti_jitter_mbox_;
public:
	anti_jitter_subscriber(context_t ctx,
		// Уникальное имя, которое будет использовать агент.
		std::string name,
		// Значение порога, которое будет использоваться для
		// отсечения "лишних" сообщений.
		clock_type::duration jitter_threshold)
		: so_5::agent_t{std::move(ctx)}
		, name_{std::move(name)}
		, anti_jitter_mbox_{
			new anti_jitter_mbox{so_direct_mbox(), jitter_threshold}}
	{
		// Подписываться нужно на новый mbox.
		so_subscribe(anti_jitter_mbox_).event([&](mhood_t<std::string> cmd) {
			std::cout << name_ << ": signal received -> " << *cmd << std::endl;
		});
	}

	// Mbox, который должен использоваться для отсылки сообщений.
	auto target_mbox() const { return anti_jitter_mbox_; }
};


// Вспомогательная функция для генерации последовательности отложенных сообщений.
void generate_msg_sequence(
		so_5::environment_t & env,
		const so_5::mbox_t & ordinary_mbox,
		const so_5::mbox_t & anti_jitter_mbox) {

	std::vector<milliseconds> delays{ 125ms, 250ms, 400ms, 500ms, 700ms, 750ms, 800ms };

	for(const auto d : delays) {
		const std::string msg = std::to_string(d.count()) + "ms";
		so_5::send_delayed<std::string>(env, ordinary_mbox, d, msg);
		so_5::send_delayed<std::string>(env, anti_jitter_mbox, d, msg);
	}
}

int main() {
	// Запускаем SObjectizer и выполняем нужные действия.
	so_5::launch([](so_5::environment_t & env) {
		// Нам нужно два mbox-а. Свои актуальные значения эти переменные
		// получат в процессе создания агентов.
		so_5::mbox_t ordinary, anti_jitter;

		// Теперь создадим двух агентов, каждый из которых будет слушать
		// собственный mbox.
		env.introduce_coop([&](so_5::coop_t & coop) {
			ordinary = coop.make_agent<ordinary_subscriber>(
					"ordinary-mbox")->target_mbox();
			anti_jitter = coop.make_agent<anti_jitter_subscriber>(
					"anti-jitter-mbox", 250ms)->target_mbox();
		});

		// Теперь нужно сгенерировать последовательность сообщений.
		generate_msg_sequence(env, ordinary, anti_jitter);
		// И дать достаточно времени для работы примера.
		std::this_thread::sleep_for(1250ms);

		// Теперь пример можно завершить.
		env.stop();
	});

	return 0;
}

