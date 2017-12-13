#include <so_5/all.hpp>

class A final : public so_5::agent_t {
	const so_5::mbox_t to_;
public:
	A(context_t ctx, so_5::mbox_t to)
		: so_5::agent_t{std::move(ctx)}, to_{std::move(to)} {}

	virtual void so_evt_start() override {
		// Отсылаем сообщение агенту B.
		so_5::send<std::string>(to_, "Hello!");
	}
};

class B final : public so_5::agent_t {
public:
	B(context_t ctx, const so_5::mbox_t & from)
		: so_5::agent_t{std::move(ctx)}
	{
		// Подписываемся на входящие сообщения из ящика from.
		so_subscribe(from).event(&B::on_string);
	}

private:
	void on_string(mhood_t<std::string> cmd) {
		std::cout << "(B) Message: " << *cmd << std::endl;
		
		// Работу можно прекращать.
		so_deregister_agent_coop_normally();
	}
};

class C final : public so_5::agent_t {
public:
	C(context_t ctx, const so_5::mbox_t & from)
		: so_5::agent_t{std::move(ctx)}
	{
		// Подписываемся на входящие сообщения из ящика from.
		so_subscribe(from).event([](mhood_t<std::string> cmd) {
			// Просто печатаем содержимое, но работу не прекращаем,
			// за нас это сделает агент B.
			std::cout << "(C) Message: " << *cmd << std::endl;
		});
	}
};

int main() {
	so_5::launch([](so_5::environment_t & env) {
		// В примере будет работать несколько агентов, которым нужен
		// общий mbox для взаимодействия.
		env.introduce_coop([&](so_5::coop_t & coop) {
			// Создаем mbox, посредством которого будут общаться
			// агенты A, B, C.
			const auto mbox = env.create_mbox();
			// Теперь создаем агентов, которые будут использовать
			// этот mbox для общения.
			coop.make_agent<A>(mbox);
			coop.make_agent<B>(mbox);
			coop.make_agent<C>(mbox);
		});
	});

	return 0;
}

